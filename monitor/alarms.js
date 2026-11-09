'use strict';
// Pure timer/state logic; the UI provides a monotonic clock and validated BLE packets.
class MonitorAlarms {
  constructor(fastSeconds=10,detachedSeconds=10){this.setLimits(fastSeconds,detachedSeconds);this.connected=false;this.lost=false;this.lastPacket=null;this.state=null;this.since=null;this.episode=0;this.muted=new Set();}
  setLimits(fastSeconds,detachedSeconds){if(![fastSeconds,detachedSeconds].every(v=>Number.isInteger(v)&&v>=1&&v<=300))throw Error('Enter whole seconds from 1 to 300.');this.fastMs=fastSeconds*1000;this.detachedMs=detachedSeconds*1000;}
  resetDuration(){this.state=null;this.since=null;this.episode++;this.muted.clear();}
  receive(state,running,now){if(this.lastPacket!==null&&now-this.lastPacket>=1500)this.resetDuration();this.connected=true;this.lost=false;this.lastPacket=now;const next=running&&(state===2||state===6)?state:null;if(next!==this.state){this.resetDuration();this.state=next;this.since=next===null?null:now;}}
  disconnect(intentional=false){const hadConnection=this.connected;this.connected=false;this.lastPacket=null;this.resetDuration();this.lost=!intentional&&(hadConnection||this.lost);}
  tick(now){if(this.connected&&this.lastPacket!==null&&now-this.lastPacket>=1500){this.resetDuration();this.lastPacket=null;this.lost=true;}}
  snapshot(now){this.tick(now);const elapsed=this.since===null?0:Math.max(0,now-this.since);let active=[];if(this.lost)active=[{type:'connection',title:'Connection lost',detail:'No reliable BLE updates. Reconnect to the board or wait for fresh data.'}];else if(this.state===6&&elapsed>=this.fastMs)active=[{type:'fast',title:'Fast breathing',detail:`Continuous fast breathing for ${(elapsed/1000).toFixed(1)} s · limit ${this.fastMs/1000} s.`}];else if(this.state===2&&elapsed>=this.detachedMs)active=[{type:'detached',title:'Sensor not attached',detail:`Continuous not attached state for ${(elapsed/1000).toFixed(1)} s · limit ${this.detachedMs/1000} s.`}];active=active.map(a=>({...a,key:a.type+':'+this.episode}));return{active,muted:active.length>0&&active.every(a=>this.muted.has(a.key)),elapsed,tracked:this.state,lost:this.lost};}
  toggleMute(now){const status=this.snapshot(now);if(status.muted)status.active.forEach(a=>this.muted.delete(a.key));else status.active.forEach(a=>this.muted.add(a.key));return this.snapshot(now);}
}
if(typeof module!=='undefined')module.exports=MonitorAlarms;
