const assert=require('assert');
const MonitorAlarms=require('../monitor/alarms.js');
const alarms=new MonitorAlarms();
const active=(time)=>alarms.snapshot(time).active.map(a=>a.type);
// An isolated brief episode must not accumulate into a later episode.
alarms.receive(6,true,0);for(let t=1000;t<=9000;t+=1000)alarms.receive(6,true,t);
assert.deepEqual(active(9999),[]);alarms.receive(5,true,10000);assert.deepEqual(active(10000),[]);
alarms.receive(6,true,11000);for(let t=12000;t<=21000;t+=1000)alarms.receive(6,true,t);
assert.deepEqual(active(21000),['fast']);assert.equal(alarms.snapshot(21000).elapsed,10000);
assert.equal(alarms.toggleMute(21000).muted,true);alarms.receive(6,true,22000);assert.equal(alarms.snapshot(22000).muted,true);
alarms.receive(5,true,23000);assert.deepEqual(active(23000),[]);
alarms.receive(2,true,24000);for(let t=25000;t<=34000;t+=1000)alarms.receive(2,true,t);
assert.deepEqual(active(34000),['detached']);assert.equal(alarms.snapshot(34000).muted,false,'A different alarm episode must be audible');
alarms.disconnect(false);assert.deepEqual(active(34000),['connection']);
alarms.toggleMute(34000);assert.equal(alarms.snapshot(50000).muted,true,'Disconnected alarm stays muted, without retriggering');
alarms.receive(2,true,50000);assert.deepEqual(active(50000),[]);assert.equal(alarms.snapshot(50000).elapsed,0);
alarms.receive(2,true,51000);assert.deepEqual(active(52499),[]);assert.deepEqual(active(52500),['connection']);
assert.equal(alarms.snapshot(52500).tracked,null,'Stale data must reset the duration timer');
alarms.receive(2,true,53000);assert.equal(alarms.snapshot(53000).elapsed,0);
alarms.disconnect(true);assert.deepEqual(active(60000),[],'Intentional disconnect must not alarm');
// No watchdog callback between two packets (background throttling) must not
// falsely count the missing interval as continuous movement.
alarms.receive(6,true,70000);alarms.receive(6,true,90000);assert.equal(alarms.snapshot(90000).elapsed,0);
alarms.receive(0,false,90500);assert.deepEqual(active(90500),[]);assert.equal(alarms.snapshot(90500).tracked,null);
alarms.receive(1,true,91000);assert.equal(alarms.snapshot(91000).tracked,null,'Warm-up must not accrue duration');
alarms.receive(4,true,91500);assert.equal(alarms.snapshot(91500).tracked,null,'Sensor faults must not accrue duration');
alarms.setLimits(2,3);alarms.receive(6,true,92000);alarms.receive(6,true,93000);alarms.receive(6,true,94000);assert.deepEqual(active(94000),['fast']);
alarms.setLimits(20,30);assert.deepEqual(active(94000),[],'Raising the limit must immediately reevaluate the current episode');
for(const value of [0,-1,301,1.5,NaN,Infinity])assert.throws(()=>alarms.setLimits(value,10));
assert.deepEqual(new MonitorAlarms().snapshot(0).active,[],'Initial disconnected page must not alarm');
console.log('Alarms: exact thresholds, continuous episodes, mute/new episodes, stop/warm-up/fault, stale gaps, reconnect, intentional disconnect and validation checks passed.');
