const fs=require('fs'),vm=require('vm'),assert=require('assert');
const elements=new Map();function element(){return{style:{},textContent:'',disabled:false,append(){},replaceChildren(){}};}
let clock=10000;
class ClockDate extends Date {static now(){return clock;}}
const sandbox={document:{body:{style:{}},getElementById(id){if(!elements.has(id))elements.set(id,element());return elements.get(id);},createElement:element},
 window:{isSecureContext:true},navigator:{bluetooth:{}},performance:{now:()=>clock},setInterval(){},Date:ClockDate,TextEncoder,console};
vm.createContext(sandbox);vm.runInContext(fs.readFileSync('monitor/alarms.js','utf8'),sandbox);vm.runInContext(fs.readFileSync('monitor/app.js','utf8'),sandbox);const run=s=>vm.runInContext(s,sandbox);
function packet(state,seq){const bytes=new Uint8Array(20),v=new DataView(bytes.buffer);bytes[0]=1;bytes[1]=state;bytes[2]=state===3||state===6?255:0;bytes[3]=state===5?255:0;bytes[4]=state===2||state===3?255:0;bytes[6]=state!==0;v.setUint32(8,seq,true);return v;}
function send(state,seq){sandbox.packet=packet(state,seq);run('receive(packet)');}
run('connected=true');send(2,1);assert.equal(sandbox.document.body.style.backgroundColor,'rgb(0, 0, 255)');assert.equal(elements.get('state').textContent,'Not attached');
send(3,2);assert.equal(sandbox.document.body.style.backgroundColor,'rgb(255, 0, 255)');assert.equal(run('history.length'),2);
send(3,3);assert.equal(run('history.length'),2,'Heartbeat must not duplicate history');
send(2,1);assert.equal(elements.get('state').textContent,'Shaking','Old packets must be ignored');
clock+=2000;run('watchdog()');assert.equal(elements.get('start').disabled,true);assert.equal(elements.get('connection').textContent,'Updates paused');
send(2,4);assert.equal(elements.get('connection').textContent,'● Bluetooth connected');
send(0,5);assert.equal(elements.get('start').disabled,false);assert.equal(elements.get('stop').disabled,true);
send(4,6);assert.equal(elements.get('state').textContent,'Sensor error');
send(5,7);assert.equal(elements.get('state').textContent,'Calm breathing');assert.equal(sandbox.document.body.style.backgroundColor,'rgb(0, 255, 0)');assert.equal(sandbox.document.body.style.color,'#052114');
send(6,8);assert.equal(elements.get('state').textContent,'Fast breathing');assert.equal(sandbox.document.body.style.backgroundColor,'rgb(255, 0, 0)');
run('disconnected()');assert.equal(elements.get('start').disabled,true);
sandbox.packet=new DataView(new ArrayBuffer(3));assert.throws(()=>run('parsePacket(packet)'));
(async()=>{let command;run('connected=true;stale=false');sandbox.control={writeValueWithResponse:async bytes=>command=Buffer.from(bytes).toString()};run('controlCharacteristic=control');await run("command('START')");assert.equal(command,'START');await run("command('STOP')");assert.equal(command,'STOP');
let tonesStarted=0;
const param={setValueAtTime(){},linearRampToValueAtTime(){}};
sandbox.window.AudioContext=class{constructor(){this.state='running';this.currentTime=0;this.destination={};}createOscillator(){return{frequency:param,connect(){},start(){tonesStarted++;},stop(){}};}createGain(){return{gain:param,connect(){}};}};
await run('primeSound()');
run('alarmManager.disconnect(true);renderAlarms();connected=true;lastSequence=null;');
clock=20000;send(6,1);for(let i=1;i<=10;i++){clock=20000+i*1000;send(6,i+1);}
assert.equal(elements.get('alarmBanner').hidden,false);assert.match(elements.get('alarmTitle').textContent,/Fast breathing/);
assert.equal(sandbox.document.body.style.backgroundColor,'rgb(255, 0, 0)','Alarm must preserve the LED-matching screen color');
assert.equal(tonesStarted,2,'Active alarm must schedule an audible two-tone signal');
run("$('muteAlarm').onclick()");assert.match(elements.get('muteStatus').textContent,/Sound muted/);
clock+=1000;send(6,12);assert.equal(tonesStarted,2,'Muted alarm must not repeat audio');
send(5,13);assert.equal(elements.get('alarmBanner').hidden,true);
clock+=1000;send(2,14);for(let i=1;i<=10;i++){clock+=1000;send(2,i+14);}
assert.match(elements.get('alarmTitle').textContent,/not attached/);assert.equal(tonesStarted,4,'A new alarm episode must sound again');
run('disconnected()');assert.match(elements.get('alarmTitle').textContent,/Connection lost/);assert.equal(elements.get('alarmBanner').hidden,false);
run('intentionalDisconnect=true;disconnected()');assert.equal(elements.get('alarmBanner').hidden,true);
elements.get('fastLimit').value='0';elements.get('detachedLimit').value='10';sandbox.submit={preventDefault(){}};
run("$('alarmSettings').onsubmit(submit)");assert.match(elements.get('settingsStatus').textContent,/1 to 300/);
elements.get('fastLimit').value='2';elements.get('detachedLimit').value='3';run("$('alarmSettings').onsubmit(submit)");assert.equal(run('alarmManager.fastMs'),2000);assert.equal(run('alarmManager.detachedMs'),3000);
run('connected=true;lastSequence=null;');clock+=1000;send(2,1);const before=run('lastSeen');clock+=1000;send(2,1);assert.equal(run('lastSeen'),before,'Duplicate packets cannot keep a dead stream fresh');clock+=500;run('watchdog()');assert.match(elements.get('alarmTitle').textContent,/Connection lost/);
console.log('BLE monitor: RGB, commands, history, stale data, visible/audible duration/loss alarms, mute/rearm, limits and duplicate-packet checks passed.');})().catch(e=>{console.error(e);process.exitCode=1;});
