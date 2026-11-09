const fs=require('fs'),vm=require('vm'),assert=require('assert');
const elements=new Map();function element(){return{style:{},textContent:'',disabled:false,append(){},replaceChildren(){}};}
let clock=10000;
class ClockDate extends Date {static now(){return clock;}}
const sandbox={document:{body:{style:{}},getElementById(id){if(!elements.has(id))elements.set(id,element());return elements.get(id);},createElement:element},
 window:{isSecureContext:true},navigator:{bluetooth:{}},setInterval(){},Date:ClockDate,TextEncoder,console};
vm.createContext(sandbox);vm.runInContext(fs.readFileSync('monitor/app.js','utf8'),sandbox);const run=s=>vm.runInContext(s,sandbox);
function packet(state,seq){const bytes=new Uint8Array(20),v=new DataView(bytes.buffer);bytes[0]=1;bytes[1]=state;bytes[2]=state===3?255:0;bytes[4]=state===2||state===3?255:0;bytes[6]=state!==0;v.setUint32(8,seq,true);return v;}
function send(state,seq){sandbox.packet=packet(state,seq);run('receive(packet)');}
run('connected=true');send(2,1);assert.equal(sandbox.document.body.style.backgroundColor,'rgb(0, 0, 255)');assert.equal(elements.get('state').textContent,'Not attached');
send(3,2);assert.equal(sandbox.document.body.style.backgroundColor,'rgb(255, 0, 255)');assert.equal(run('history.length'),2);
send(3,3);assert.equal(run('history.length'),2,'Heartbeat must not duplicate history');
send(2,1);assert.equal(elements.get('state').textContent,'Shaking','Old packets must be ignored');
clock+=2000;run('watchdog()');assert.equal(elements.get('start').disabled,true);assert.equal(elements.get('connection').textContent,'Updates paused');
send(2,4);assert.equal(elements.get('connection').textContent,'● Bluetooth connected');
send(0,5);assert.equal(elements.get('start').disabled,false);assert.equal(elements.get('stop').disabled,true);
send(4,6);assert.equal(elements.get('state').textContent,'Sensor error');run('disconnected()');assert.equal(elements.get('start').disabled,true);
sandbox.packet=new DataView(new ArrayBuffer(3));assert.throws(()=>run('parsePacket(packet)'));
(async()=>{let command;run('connected=true;stale=false');sandbox.control={writeValueWithResponse:async bytes=>command=Buffer.from(bytes).toString()};run('controlCharacteristic=control');await run("command('START')");assert.equal(command,'START');await run("command('STOP')");assert.equal(command,'STOP');console.log('BLE monitor: exact RGB colors, command encoding, history, stale/reordered packets, faults and disconnect checks passed.');})().catch(e=>{console.error(e);process.exitCode=1;});
