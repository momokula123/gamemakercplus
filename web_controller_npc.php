<?php
// NPC Controller PHP版本
// 用于远程服务器部署
$proxyUrl = 'http://你的服务器IP/npc_proxy.php'; // 修改为你的PHP代理地址
?>
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
<title>NPC Controller (PHP)</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{--bg:#0a0a0f;--panel:#12121a;--card:#1a1a28;--border:#2a2a3e;--text:#e0e0f0;--dim:#6a6a8e;--accent:#4a7cff;--green:#2dd4a0;--red:#ff4466}
html,body{height:100%;background:var(--bg);color:var(--text);font-family:'Segoe UI','Microsoft YaHei',sans-serif;overflow:hidden}
.app{height:100%;display:flex;flex-direction:column;max-width:520px;margin:0 auto;padding:12px;gap:8px}
.header{text-align:center;padding:8px 0 4px}
.header h1{font-size:18px;font-weight:700;background:linear-gradient(135deg,var(--accent),var(--green));-webkit-background-clip:text;-webkit-text-fill-color:transparent}
.npc-panel{background:var(--panel);border:1px solid var(--border);border-radius:10px;padding:10px}
.npc-panel label{font-size:12px;color:var(--dim);display:block;margin-bottom:4px}
.npc-panel select{width:100%;padding:8px;background:var(--card);color:var(--text);border:1px solid var(--border);border-radius:6px;font-size:13px}
.npc-panel .row{display:flex;gap:6px;margin-top:8px}
.npc-panel .info{font-size:11px;color:var(--dim);margin-top:6px;min-height:16px}
.dpad-area{flex:1;display:flex;align-items:center;justify-content:center;position:relative}
.dpad-container{position:relative;width:200px;height:200px}
.dpad-btn{position:absolute;width:60px;height:60px;background:var(--card);border:2px solid var(--border);border-radius:14px;display:flex;align-items:center;justify-content:center;cursor:pointer;user-select:none;-webkit-tap-highlight-color:transparent;touch-action:manipulation;font-size:20px;transition:transform 0.1s ease}
.dpad-up{top:0;left:50%;transform:translateX(-50%)}
.dpad-up:active,.dpad-up.active{transform:translateX(-50%) translateY(3px);background:var(--accent);border-color:var(--accent)}
.dpad-down{bottom:0;left:50%;transform:translateX(-50%)}
.dpad-down:active,.dpad-down.active{transform:translateX(-50%) translateY(3px);background:var(--accent);border-color:var(--accent)}
.dpad-left{left:0;top:50%;transform:translateY(-50%)}
.dpad-left:active,.dpad-left.active{transform:translateY(-50%) translateY(3px);background:var(--accent);border-color:var(--accent)}
.dpad-right{right:0;top:50%;transform:translateY(-50%)}
.dpad-right:active,.dpad-right.active{transform:translateY(-50%) translateY(3px);background:var(--accent);border-color:var(--accent)}
.dpad-center{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);width:46px;height:46px;border-radius:50%;background:var(--panel);border:2px solid var(--border);display:flex;align-items:center;justify-content:center;font-size:9px;color:var(--dim);cursor:pointer}
.bottom-bar{display:flex;gap:6px;padding-bottom:env(safe-area-inset-bottom,8px)}
.bottom-bar button{flex:1;padding:10px;border-radius:8px;border:1px solid var(--border);background:var(--card);color:var(--text);font-size:12px;cursor:pointer}
.bottom-bar button:active{transform:scale(0.96)}
.log{background:var(--panel);border:1px solid var(--border);border-radius:8px;padding:6px 10px;max-height:60px;overflow-y:auto;font-size:10px;font-family:Consolas,monospace;color:var(--dim)}
</style>
</head>
<body>
<div class="app">
  <div class="header"><h1>NPC Controller (PHP)</h1></div>
  <div class="npc-panel">
    <label>Select NPC</label>
    <select id="npcSelect"><option value="-1">-- click Refresh --</option></select>
    <div class="info" id="npcInfo"></div>
    <div class="row">
      <button onclick="refreshList()">Refresh List</button>
      <button onclick="showInfo()">Info</button>
    </div>
  </div>
  <div class="dpad-area">
    <div class="dpad-container">
      <div class="dpad-btn dpad-up" data-dir="npc_up">&#9650;</div>
      <div class="dpad-btn dpad-down" data-dir="npc_down">&#9660;</div>
      <div class="dpad-btn dpad-left" data-dir="npc_left">&#9664;</div>
      <div class="dpad-btn dpad-right" data-dir="npc_right">&#9654;</div>
      <div class="dpad-center" onclick="doStop()">STOP</div>
    </div>
  </div>
  <div class="bottom-bar">
    <button onclick="fire('pos')">Player Pos</button>
    <button onclick="refreshList()">List</button>
    <button style="border-color:var(--red);color:var(--red)" onclick="if(confirm('Quit?'))fire('quit')">Quit</button>
  </div>
  <div class="log" id="log"></div>
</div>
<script>
// 修改为你的PHP代理地址
const API='<?php echo $proxyUrl; ?>';
let activeDir=null;
let dirInterval=null;
let selectedNpcId=-1;

// 使用fetch发送请求（通过PHP代理）
function fire(cmd){
    fetch(API+'?cmd='+cmd)
    .then(r=>r.text())
    .then(t=>log(cmd,t))
    .catch(e=>log(cmd,e.message,true));
}

function log(cmd,res,err){
    const el=document.getElementById('log');
    el.innerHTML+='<div>'+new Date().toLocaleTimeString('zh-CN',{hour12:false})+' <b style="color:var(--accent)">'+cmd+'</b> -> <span style="color:'+(err?'var(--red)':'var(--green)')+'">'+res+'</span></div>';
    el.scrollTop=el.scrollHeight;
}

async function refreshList(){
    try{
        const r=await fetch(API+'?cmd=npc_list',{signal:AbortSignal.timeout(3000)});
        const t=await r.text();
        log('npc_list',t);
        const sel=document.getElementById('npcSelect');
        const prev=sel.value;
        sel.innerHTML='<option value="-1">-- No NPC --</option>';
        t.split('\n').filter(l=>l.startsWith('id=')).forEach(line=>{
            const m=line.match(/id=(\d+)\|(.+?)\|(.+?)\|/);
            if(m){const o=document.createElement('option');o.value=m[1];o.textContent='#'+m[1]+' '+m[2]+' ['+m[3]+']';sel.appendChild(o);}
        });
        if(sel.options.length>1){
            if(prev!=='-1'){sel.value=prev;if(sel.value==='-1')sel.value=sel.options[1].value;}
            else{sel.value=sel.options[1].value;}
            doSelect();
        }
    }catch(e){log('npc_list',e.message,true);}
}

function doSelect(){
    const id=document.getElementById('npcSelect').value;
    selectedNpcId=parseInt(id);
    if(id!=='-1'){
        fire('npc_select&id='+id);
        document.getElementById('npcInfo').textContent='Selected #'+id;
    }else{
        document.getElementById('npcInfo').textContent='';
    }
}

async function showInfo(){
    const id=document.getElementById('npcSelect').value;
    if(id==='-1')return;
    try{
        const r=await fetch(API+'?cmd=npc_info&id='+id,{signal:AbortSignal.timeout(3000)});
        const t=await r.text();
        document.getElementById('npcInfo').textContent=t;
    }catch(e){}
}

function startDir(dir){
    const sel=document.getElementById('npcSelect');
    if(sel.value==='-1'&&sel.options.length>1){sel.value=sel.options[1].value;doSelect();}
    if(sel.value==='-1'){log(dir,'no NPC selected',true);return;}
    activeDir=dir;
    fire(dir);
    clearInterval(dirInterval);
    dirInterval=setInterval(()=>{if(activeDir)fire(activeDir);},150);
}

function stopDir(){
    activeDir=null;
    clearInterval(dirInterval);
    dirInterval=null;
    fire('npc_stop');
}

function doStop(){stopDir();}

document.getElementById('npcSelect').addEventListener('change',doSelect);
document.querySelectorAll('.dpad-btn').forEach(el=>{
    const dir=el.dataset.dir;
    el.addEventListener('pointerdown',e=>{e.preventDefault();pressed[dir]=true;el.classList.add('active');startDir(dir);});
    el.addEventListener('pointerup',()=>{if(pressed[dir]){delete pressed[dir];el.classList.remove('active');stopDir();}});
    el.addEventListener('pointerleave',()=>{if(pressed[dir]){delete pressed[dir];el.classList.remove('active');stopDir();}});
    el.addEventListener('pointercancel',()=>{if(pressed[dir]){delete pressed[dir];el.classList.remove('active');stopDir();}});
});
const pressed={};
const kmap={ArrowUp:'npc_up',ArrowDown:'npc_down',ArrowLeft:'npc_left',ArrowRight:'npc_right'};
document.addEventListener('keydown',e=>{const d=kmap[e.key];if(d){e.preventDefault();if(!pressed[d]){pressed[d]=true;startDir(d);}}});
document.addEventListener('keyup',e=>{const d=kmap[e.key];if(d){e.preventDefault();if(pressed[d]){delete pressed[d];stopDir();}}});
refreshList();setInterval(refreshList,3000);
</script>
</body>
</html>