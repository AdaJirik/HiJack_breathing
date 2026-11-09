"""Recording-level split, compact feature models and dependency-free C++ export."""
from pathlib import Path
import csv
import hashlib
import json
import warnings

import numpy as np
from sklearn.exceptions import ConvergenceWarning
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import classification_report, confusion_matrix, f1_score
from sklearn.neural_network import MLPClassifier
from sklearn.model_selection import StratifiedGroupKFold, cross_val_score
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.tree import DecisionTreeClassifier

ROOT = Path(__file__).resolve().parents[1]
LABELS = ["not_attached", "shaking"]
COLUMNS = ["ax_m_s2", "ay_m_s2", "az_m_s2", "gx_deg_s", "gy_deg_s", "gz_deg_s"]
FEATURES = ["log_accel_std", "log_gyro_std", "log_accel_magnitude_std", "log_accel_step_rms", "log_gyro_step_rms", "log_gyro_rms"]
WINDOW, STRIDE, SEED = 100, 25, 42

def features(window):
    a, g = window[:, :3], window[:, 3:]
    values = [np.sqrt(np.sum(np.var(a, axis=0))), np.sqrt(np.sum(np.var(g, axis=0))),
              np.std(np.linalg.norm(a, axis=1)),
              np.sqrt(np.mean(np.sum(np.diff(a, axis=0)**2, axis=1))),
              np.sqrt(np.mean(np.sum(np.diff(g, axis=0)**2, axis=1))),
              np.sqrt(np.mean(np.sum(g*g, axis=1)))]
    return np.log1p(np.asarray(values, dtype=np.float32))

def main():
    output = ROOT / "models"
    output.mkdir(exist_ok=True)
    recordings, quality, sessions, digests = {}, [], set(), set()
    for label in LABELS:
        recordings[label] = []
        for path in sorted((ROOT/"Data").glob(f"*_{label}.csv")):
            with path.open(newline="", encoding="utf-8-sig") as stream:
                rows = list(csv.DictReader(stream))
            values = np.array([[float(r[k]) for k in COLUMNS] for r in rows], dtype=np.float32)
            timestamps = np.array([int(r["timestamp_us"]) for r in rows], dtype=np.int64)
            session = rows[0]["session_id"]
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if session in sessions or digest in digests:
                raise ValueError(f"Duplicate recording/session: {path.name}; group before splitting")
            sessions.add(session); digests.add(digest)
            if not np.isfinite(values).all() or np.any(np.diff(timestamps)<=0) or len(rows)<WINDOW:
                raise ValueError(f"Invalid samples/timestamps in {path.name}")
            dt = np.diff(timestamps)
            if abs(np.median(dt)-10000)>1000:
                raise ValueError(f"Unexpected sampling rate in {path.name}")
            quality.append(dict(file=path.name, label=label, rows=len(rows), duration_s=float((timestamps[-1]-timestamps[0])/1e6),
                median_dt_us=float(np.median(dt)), max_dt_us=int(dt.max()), sha256=digest,
                clipped_samples=int(np.sum(np.any(np.abs(values[:,:3])>19.55,axis=1)|np.any(np.abs(values[:,3:])>249,axis=1)))))
            recordings[label].append(dict(file=path.name, values=values, session=session))
    split = {name: [] for name in ("train", "validation", "test")}
    rng = np.random.default_rng(SEED)
    for label in LABELS:
        group=recordings[label]
        if len(group)<5: raise ValueError("Need at least five independent recordings per class")
        order=rng.permutation(len(group)); n_test=max(1,round(len(group)*.2)); n_val=n_test
        for i,index in enumerate(order):
            name="test" if i<n_test else "validation" if i<n_test+n_val else "train"
            split[name].append(group[index]["file"])
    all_recordings={r["file"]:(r,label) for label,rs in recordings.items() for r in rs}
    def dataset(names):
        windows, labels, ids = [], [], []
        for name in names:
            rec,label=all_recordings[name]
            for start in range(0,len(rec["values"])-WINDOW+1,STRIDE):
                windows.append(rec["values"][start:start+WINDOW]);labels.append(LABELS.index(label));ids.append(name)
        windows=np.asarray(windows)
        return np.array([features(w) for w in windows]),np.array(labels),windows,ids
    train=dataset(split["train"]); val=dataset(split["validation"])
    candidates={"logistic_regression":make_pipeline(StandardScaler(),LogisticRegression(C=1,random_state=SEED)),
                "mlp_16":make_pipeline(StandardScaler(),MLPClassifier(hidden_layer_sizes=(16,),solver="lbfgs",alpha=.1,max_iter=2000,random_state=SEED)),
                "decision_stump":make_pipeline(StandardScaler(),DecisionTreeClassifier(max_depth=1,min_samples_leaf=10,random_state=SEED))}
    scores={}
    with warnings.catch_warnings():
        warnings.simplefilter("error",ConvergenceWarning)
        for name,pipe in candidates.items():
            pipe.fit(train[0],train[1]);scores[name]=float(f1_score(val[1],pipe.predict(val[0]),average="macro"))
    fit=dataset(split["train"]+split["validation"])
    # A single four-recording validation split is noisy. Select by grouped CV
    # over the development recordings; the test recordings remain excluded.
    cv=StratifiedGroupKFold(n_splits=4,shuffle=True,random_state=SEED)
    cv_scores={name:cross_val_score(pipe,fit[0],fit[1],groups=fit[3],cv=cv,scoring="f1_macro").tolist() for name,pipe in candidates.items()}
    chosen=max(cv_scores,key=lambda name:(np.mean(cv_scores[name]),name=="logistic_regression"))
    model=candidates[chosen].fit(fit[0],fit[1]); test=dataset(split["test"])
    predictions=model.predict(test[0]); probabilities=model.predict_proba(test[0])[:,1]
    scaler, estimator=model.steps[0][1],model.steps[-1][1]
    tree=None
    if chosen=="decision_stump":
        t=estimator.tree_
        tree=dict(feature=t.feature.tolist(),threshold=t.threshold.tolist(),left=t.children_left.tolist(),right=t.children_right.tolist(),
                  probability=[float(v[0,1]/v[0].sum()) for v in t.value])
        weights=[];biases=[]
    elif chosen=="logistic_regression":
        weights=[estimator.coef_.T];biases=[estimator.intercept_]
    else: weights=estimator.coefs_;biases=estimator.intercepts_
    params={"kind":chosen,"labels":LABELS,"features":FEATURES,"window_samples":WINDOW,"stride_samples":STRIDE,
        "sampling_hz":100,"mean":scaler.mean_.tolist(),"scale":scaler.scale_.tolist(),
        "weights":[w.tolist() for w in weights],"biases":[b.tolist() for b in biases]}
    if tree:params["tree"]=tree
    report={"seed":SEED,"protocol":"Split whole recordings before overlapping windows; compare initial validation and select model with four-fold recording-group CV over train+validation; refit development data; evaluate test recordings excluded from all selection fits.",
        "selection_macro_f1":scores,"selected":chosen,"window_samples":WINDOW,"stride_samples":STRIDE,
        "development_group_cv_macro_f1":cv_scores,
        "test_windows":len(test[1]),"test_macro_f1":float(f1_score(test[1],predictions,average="macro")),
        "test_confusion_matrix":confusion_matrix(test[1],predictions).tolist(),
        "test_report":classification_report(test[1],predictions,target_names=LABELS,output_dict=True),
        "recording_results":[dict(file=name,windows=test[3].count(name),accuracy=float(np.mean(predictions[np.array(test[3])==name]==test[1][np.array(test[3])==name]))) for name in split["test"]],
        "limitations":"20 short recordings from one collection session; no verified subject identities, no cross-person test, and no unseen-activity rejection. Breathing files deliberately excluded. Window accuracy is not live transition accuracy."}
    for name,value in [("movement_model.json",params),("movement_report.json",report),("movement_split.json",split),("movement_data_quality.json",quality)]:
        (output/name).write_text(json.dumps(value,indent=2)+"\n",encoding="utf-8")
    def c_array(name,array):
        flat=np.asarray(array).ravel()
        return f"static const float {name}[{len(flat)}]={{"+",".join(f"{float(v):.9e}f" for v in flat)+"};\n"
    header="#pragma once\n#include <math.h>\n// Generated by training/train_movement.py. Class 1 is shaking.\n"
    header+=f'constexpr const char* MOVEMENT_MODEL_VERSION="movement-v1-{chosen}";\n'
    header+=c_array("MODEL_MEAN",scaler.mean_)+c_array("MODEL_SCALE",scaler.scale_)
    for i,(w,b) in enumerate(zip(weights,biases)):header+=c_array(f"MODEL_W{i}",w)+c_array(f"MODEL_B{i}",b)
    header+="inline float movementProbability(const float* features) {\nfloat x[6];for(int i=0;i<6;i++)x[i]=(features[i]-MODEL_MEAN[i])/MODEL_SCALE[i];\n"
    if tree:
        f,threshold=tree["feature"][0],tree["threshold"][0]
        lp,rp=tree["probability"][tree["left"][0]],tree["probability"][tree["right"][0]]
        header+=f"return x[{f}]<={threshold:.9e}f?{lp:.9e}f:{rp:.9e}f;\n}}\n"
    elif chosen=="logistic_regression":header+="float z=MODEL_B0[0];for(int i=0;i<6;i++)z+=x[i]*MODEL_W0[i];\n"
    else:header+="float h[16];for(int j=0;j<16;j++){h[j]=MODEL_B0[j];for(int i=0;i<6;i++)h[j]+=x[i]*MODEL_W0[i*16+j];h[j]=fmaxf(0,h[j]);}float z=MODEL_B1[0];for(int j=0;j<16;j++)z+=h[j]*MODEL_W1[j];\n"
    if not tree:header+="return z>=0?1.f/(1.f+expf(-z)):expf(z)/(1.f+expf(z));\n}\n"
    (ROOT/"firmware/include/movement_model.h").write_text(header,encoding="utf-8")
    # Reference windows for checking the actual C++ features and exported model.
    refs=[0,len(test[1])//2,len(test[1])-1]
    with (output/"movement_reference.csv").open("w",newline="") as stream:
        writer=csv.writer(stream);writer.writerow(["reference","expected_probability",*COLUMNS])
        for index in refs:
            for row in test[2][index]:writer.writerow([index,float(probabilities[index]),*row.tolist()])
    print(json.dumps({"selected":chosen,"validation":scores,"test_macro_f1":report["test_macro_f1"],"confusion_matrix":report["test_confusion_matrix"],"files":{k:len(v) for k,v in split.items()},"clipped_samples":sum(r["clipped_samples"] for r in quality)},indent=2))

if __name__=="__main__": main()
