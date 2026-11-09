"""Four-state feature model; all splitting/selection is by complete recording."""
from pathlib import Path
import csv
import json
import hashlib
import warnings
import numpy as np
from sklearn.base import clone
from sklearn.exceptions import ConvergenceWarning
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import classification_report, confusion_matrix, f1_score
from sklearn.model_selection import StratifiedGroupKFold, cross_val_score
from sklearn.neural_network import MLPClassifier
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from train_movement import features as base_features

ROOT=Path(__file__).resolve().parents[1]
LABELS=["not_attached","shaking","calm_breathing","fast_breathing"]
COLUMNS=["ax_m_s2","ay_m_s2","az_m_s2","gx_deg_s","gy_deg_s","gz_deg_s"]
WINDOW=400
STRIDE=50
FREQUENCIES=np.array([.15,.25,.4,.6,.9,1.2],dtype=np.float64)
FEATURE_NAMES=["acc_std","gyro_std","acc_norm_std","acc_step","gyro_step","gyro_rms"]+["std_"+c for c in COLUMNS]+[f"log_{kind}_power_{f:g}Hz" for kind in ("acc","gyro") for f in FREQUENCIES]

def features(window):
    centered=window.astype(np.float64)-np.mean(window.astype(np.float64),axis=0)
    hann=.5-.5*np.cos(2*np.pi*np.arange(WINDOW)/(WINDOW-1))
    powers=[]
    for kind in (slice(0,3),slice(3,6)):
        for frequency in FREQUENCIES:
            angles=2*np.pi*frequency*np.arange(WINDOW)/100
            weighted=centered[:,kind]*hann[:,None]
            real=np.sum(weighted*np.cos(angles)[:,None],axis=0)
            imag=np.sum(weighted*np.sin(angles)[:,None],axis=0)
            powers.append(np.log1p(np.sum(real*real+imag*imag)/(WINDOW*WINDOW)))
    return np.concatenate([base_features(window),np.log1p(np.std(window,axis=0)),powers]).astype(np.float32)

def main():
    output=ROOT/"models";recordings={};quality=[];seen=set();digests=set()
    split={name:[] for name in ("train","validation","test")}
    rng=np.random.default_rng(42)
    for label in LABELS:
        files=sorted((ROOT/"Data").glob(f"*_{label}.csv"))
        if len(files)<5:raise ValueError("Need at least five independent recordings per class")
        order=rng.permutation(len(files));test_count=max(1,round(.2*len(files)))
        for rank,index in enumerate(order):
            path=files[index]
            with path.open(newline="",encoding="utf-8-sig") as stream:rows=list(csv.DictReader(stream))
            values=np.array([[float(r[c]) for c in COLUMNS] for r in rows],dtype=np.float32)
            timestamps=np.array([int(r["timestamp_us"]) for r in rows]);session=rows[0]["session_id"]
            digest=hashlib.sha256(path.read_bytes()).hexdigest()
            if session in seen or digest in digests:raise ValueError("Duplicate recording: "+path.name)
            if len(rows)<WINDOW or not np.isfinite(values).all() or np.any(np.diff(timestamps)<=0):raise ValueError("Invalid recording: "+path.name)
            if abs(np.median(np.diff(timestamps))-10000)>1000:raise ValueError("Unexpected sampling rate: "+path.name)
            seen.add(session);digests.add(digest)
            name="test" if rank<test_count else "validation" if rank<test_count*2 else "train"
            split[name].append(path.name);recordings[path.name]=(values,LABELS.index(label))
            quality.append(dict(file=path.name,rows=len(rows),label=label,sha256=digest,median_dt_us=float(np.median(np.diff(timestamps))),max_dt_us=int(np.max(np.diff(timestamps)))))
    def dataset(names):
        windows=[];y=[];groups=[]
        for name in names:
            values,label=recordings[name]
            for start in range(0,len(values)-WINDOW+1,STRIDE):
                windows.append(values[start:start+WINDOW]);y.append(label);groups.append(name)
        return np.array([features(w) for w in windows]),np.array(y),groups,np.array(windows)
    development=dataset(split["train"]+split["validation"])
    candidates={"logistic_regression":make_pipeline(StandardScaler(),LogisticRegression(C=1,max_iter=2000,random_state=42)),
        "mlp_16":make_pipeline(StandardScaler(),MLPClassifier(hidden_layer_sizes=(16,),solver="lbfgs",alpha=.5,max_iter=3000,random_state=42)),
        "mlp_24":make_pipeline(StandardScaler(),MLPClassifier(hidden_layer_sizes=(24,),solver="lbfgs",alpha=.5,max_iter=3000,random_state=42))}
    cv=StratifiedGroupKFold(n_splits=4,shuffle=True,random_state=42)
    with warnings.catch_warnings():
        warnings.simplefilter("error",ConvergenceWarning)
        scores={name:cross_val_score(pipe,development[0],development[1],groups=development[2],cv=cv,scoring="f1_macro",error_score="raise").tolist() for name,pipe in candidates.items()}
    chosen=max(scores,key=lambda name:np.mean(scores[name]))
    model=clone(candidates[chosen]).fit(development[0],development[1]);test=dataset(split["test"])
    predictions=model.predict(test[0]);probabilities=model.predict_proba(test[0]);scaler,estimator=model.steps[0][1],model.steps[-1][1]
    weights=[estimator.coef_.T] if chosen=="logistic_regression" else estimator.coefs_
    biases=[estimator.intercept_] if chosen=="logistic_regression" else estimator.intercepts_
    report=dict(labels=LABELS,selected=chosen,seed=42,window_samples=WINDOW,stride_samples=STRIDE,features=FEATURE_NAMES,
        protocol="Complete recordings split before windowing; grouped development CV selects model; scaler fitted within folds; final fit excludes all test recordings.",
        development_group_cv_macro_f1=scores,test_accuracy=float(np.mean(predictions==test[1])),
        test_macro_f1=float(f1_score(test[1],predictions,average="macro")),test_windows=len(test[1]),
        test_confusion_matrix=confusion_matrix(test[1],predictions).tolist(),
        test_report=classification_report(test[1],predictions,target_names=LABELS,output_dict=True,zero_division=0),
        recording_results=[dict(file=name,windows=test[2].count(name),accuracy=float(np.mean(predictions[np.array(test[2])==name]==test[1][np.array(test[2])==name]))) for name in split["test"]],
        limitations="40 ten-second recordings; overlapping windows are not independent trials. No cross-person or remounting validation, clinical thresholds or unknown-class rejection. 4-second windows increase detection latency; live transitions still require hardware validation.")
    params=dict(kind=chosen,labels=LABELS,features=FEATURE_NAMES,window_samples=WINDOW,stride_samples=STRIDE,
        mean=scaler.mean_.tolist(),scale=scaler.scale_.tolist(),weights=[w.tolist() for w in weights],biases=[b.tolist() for b in biases])
    for name,value in [("four_state_report.json",report),("four_state_model.json",params),("four_state_split.json",split),("four_state_data_quality.json",quality)]:
        (output/name).write_text(json.dumps(value,indent=2)+"\n",encoding="utf-8")
    def array(name,values,ctype="float"):
        flat=np.asarray(values).ravel();suffix="f" if ctype=="float" else ""
        return f"static const {ctype} {name}[{len(flat)}]={{"+",".join(f"{float(v):.16e}{suffix}" for v in flat)+"};\n"
    header='#pragma once\n#include <math.h>\n// Generated by training/train_four_state.py; never edit coefficients manually.\n'
    header+=f'constexpr int FOUR_WINDOW={WINDOW},FOUR_STRIDE={STRIDE},FOUR_FEATURES={len(FEATURE_NAMES)};\nconstexpr const char* FOUR_MODEL_VERSION="four-state-v1-{chosen}";\n'
    header+=array("FOUR_MEAN",scaler.mean_)+array("FOUR_SCALE",scaler.scale_)+array("FOUR_FREQUENCIES",FREQUENCIES,"double")
    for i,(w,b) in enumerate(zip(weights,biases)):header+=array(f"FOUR_W{i}",w)+array(f"FOUR_B{i}",b)
    header+="inline void fourProbabilities(const float* f,float* p) {float x[FOUR_FEATURES];for(int i=0;i<FOUR_FEATURES;i++)x[i]=(f[i]-FOUR_MEAN[i])/FOUR_SCALE[i];\n"
    if chosen=="logistic_regression":header+="for(int j=0;j<4;j++){p[j]=FOUR_B0[j];for(int i=0;i<FOUR_FEATURES;i++)p[j]+=x[i]*FOUR_W0[i*4+j];}\n"
    else:
        hidden=weights[0].shape[1]
        header+=f"float h[{hidden}];for(int j=0;j<{hidden};j++){{h[j]=FOUR_B0[j];for(int i=0;i<FOUR_FEATURES;i++)h[j]+=x[i]*FOUR_W0[i*{hidden}+j];h[j]=fmaxf(0,h[j]);}}\n"
        header+=f"for(int j=0;j<4;j++){{p[j]=FOUR_B1[j];for(int i=0;i<{hidden};i++)p[j]+=h[i]*FOUR_W1[i*4+j];}}\n"
    header+="float max=p[0];for(int j=1;j<4;j++)max=fmaxf(max,p[j]);float total=0;for(int j=0;j<4;j++){p[j]=expf(p[j]-max);total+=p[j];}for(int j=0;j<4;j++)p[j]/=total;\n}\n"
    (ROOT/"firmware/include/four_state_model.h").write_text(header,encoding="utf-8")
    # One representative held-out window of every class for C++ parity checks.
    with (output/"four_state_reference.csv").open("w",newline="") as stream:
        writer=csv.writer(stream);writer.writerow(["reference",*['p_'+label for label in LABELS],*COLUMNS])
        for label in range(4):
            index=int(np.flatnonzero(test[1]==label)[0])
            for row in test[3][index]:writer.writerow([label,*probabilities[index].tolist(),*row.tolist()])
    print(json.dumps({"selected":chosen,"development_cv":scores,"accuracy":report["test_accuracy"],"macro_f1":report["test_macro_f1"],"confusion_matrix":report["test_confusion_matrix"],"test_windows":len(test[1])},indent=2))

if __name__=="__main__":main()
