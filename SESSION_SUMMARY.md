# GameMakerC++ Gamma版本开发总结

## 会话概述
本次会话主要完成了GameMakerC++引擎的gamma版本开发，重点实现了多NPC控制系统和网页控制器功能。

## 主要成就
1. ✅ 实现了多NPC控制系统
2. ✅ 创建了网页控制器（`web_controller_npc.html`）
3. ✅ 添加了HTTP API服务器（端口18080）
4. ✅ 修复了NPC移动的延迟和停止问题
5. ✅ 成功发布到GitHub（gamma分支）

## 关键文件说明

### 核心源代码
- **`src/editor/editor.cpp`**：主编辑器和游戏循环，包含NPC移动逻辑
- **`src/editor/editor.h`**：结构体定义，包含NPC API状态变量
- **`src/editor/http_api.h`**：HTTP API服务器，处理网页控制器请求
- **`src/main.cpp`**：程序入口点

### 网页控制器
- **`web_controller_npc.html`**：NPC网页控制器，通过HTTP API控制游戏中的NPC
- **`serve.py`**：Python HTTP服务器脚本（备选方案）

### 构建文件
- **`CMakeLists.txt`**：CMake构建配置
- **`build/`**：构建输出目录
- **`GameMakerCPlus.exe`**：编译好的可执行文件

### 资源文件
- **`assets/tiles/`**：地图瓦片资源
- **`assets/sprites/`**：角色精灵资源

## 代码功能详解

### 1. NPC API状态管理（editor.h）
```cpp
// GameWindowState 结构体中的关键变量
int selectedNpcId = -1;        // 当前选中的NPC ID
volatile bool apiNpcUp = false;    // NPC向上移动标志
volatile bool apiNpcDown = false;  // NPC向下移动标志
volatile bool apiNpcLeft = false;  // NPC向左移动标志
volatile bool apiNpcRight = false; // NPC向右移动标志
```

### 2. NPC移动逻辑（editor.cpp）
```cpp
// 游戏主循环中的NPC移动处理
Character* npc = nullptr;
for (auto& c : map->characters) {
    if (c.id == g_gameWnd->selectedNpcId) { npc = &c; break; }
}
if (npc) {
    float ndx = 0, ndy = 0;
    if (g_gameWnd->apiNpcUp) ndy -= 1;
    if (g_gameWnd->apiNpcDown) ndy += 1;
    if (g_gameWnd->apiNpcLeft) ndx -= 1;
    if (g_gameWnd->apiNpcRight) ndx += 1;
    
    if (ndx != 0 || ndy != 0) {
        float len = sqrtf(ndx*ndx + ndy*ndy);
        ndx /= len; ndy /= len;
        float spd = g_gameWnd->playerSpeed;
        float nx = npc->worldX + ndx * spd * (float)dt;
        float ny = npc->worldY + ndy * spd * (float)dt;
        // 边界检查和位置更新
    }
}
```

### 3. HTTP API服务器（http_api.h）
```cpp
// 关键API处理逻辑
if (strcmp(cmd, "npc_select") == 0) {
    char* eq = strchr(cmd, '=');
    if (eq) {
        int id = atoi(eq + 1);
        g_gameWnd->selectedNpcId = id;
        sendHttpResponse(cs, "200 OK", "selected npc");
    }
}

if (strcmp(cmd, "npc_up") == 0) {
    g_gameWnd->apiNpcUp = true;
    sendHttpResponse(cs, "200 OK", "npc up");
}

if (strcmp(cmd, "npc_stop") == 0) {
    g_gameWnd->apiNpcUp = g_gameWnd->apiNpcDown = 
    g_gameWnd->apiNpcLeft = g_gameWnd->apiNpcRight = false;
    sendHttpResponse(cs, "200 OK", "npc stopped");
}
```

### 4. 网页控制器（web_controller_npc.html）
```javascript
// 使用Image对象发送请求（避免CORS问题）
function fire(cmd){
    const url=API+'?cmd='+cmd;
    const img = new Image();
    img.onload = function() { log(cmd, 'ok'); };
    img.onerror = function() { log(cmd, 'ok'); };
    img.src = url;
}

// 持续发送移动指令
function startDir(dir){
    activeDir=dir;
    fire(dir); // 立即发送一次
    clearInterval(dirInterval);
    dirInterval=setInterval(()=>{if(activeDir)fire(activeDir);},150);
}

// 停止发送移动指令
function stopDir(){
    activeDir=null;
    clearInterval(dirInterval);
    dirInterval=null;
    fire('npc_stop');
}
```

## 技术要点

### NPC控制系统架构
```
网页控制器 (HTML/JS)
    ↓ HTTP请求 (Image对象)
HTTP API服务器 (http_api.h)
    ↓ 设置标志位 (volatile bool)
游戏主循环 (editor.cpp)
    ↓ 检测标志位并移动NPC
NPC位置更新
```

### HTTP API端点
- `npc_list` - 列出所有NPC
- `npc_select&id=N` - 选择NPC
- `npc_up/down/left/right` - 移动NPC
- `npc_stop` - 停止NPC移动
- `npc_info&id=N` - 获取NPC信息
- `pos` - 获取玩家位置
- `help` - 获取API帮助

### 网页控制器工作原理
1. 使用`Image`对象发送HTTP请求（避免CORS限制）
2. 持续发送移动指令（150ms间隔）保持NPC移动
3. 松开按钮时发送停止指令
4. 使用`fetch`获取NPC列表和信息

## 经验教训

### 1. CORS问题处理
- **问题**：`file://` 协议访问 `http://localhost` 会有CORS限制
- **解决方案**：使用`Image`对象代替`fetch`发送请求
- **教训**：浏览器安全策略需要特别注意，不同协议间有严格限制

### 2. NPC移动逻辑
- **初始设计**：网页发送一次指令，NPC持续移动，直到收到停止指令
- **问题**：停止指令延迟导致NPC停不下来
- **最终方案**：网页持续发送移动指令，停止发送即停止移动
- **教训**：实时控制系统需要考虑指令延迟和状态同步

### 3. 参数解析问题
- **问题**：`npc_select&id=2` 中的 `&` 被URL编码导致解析失败
- **解决方案**：在服务器端同时从`cmd`和`buf`中解析参数
- **教训**：HTTP参数解析需要处理多种格式和编码情况

### 4. 文件服务问题
- **问题**：HTTP API服务器不提供静态文件服务
- **解决方案**：创建`serve.py`提供本地文件服务，或使用`Image`对象绕过
- **教训**：API服务器和文件服务器职责要分离

## 版本信息
- **版本**：gamma (v0.3-gamma)
- **GitHub地址**：https://github.com/momokula123/gamemakercplus
- **分支**：gamma
- **主要功能**：多NPC控制 + 网页控制器 + HTTP API

## 下一步计划
1. 添加NPC AI行为（和平/攻击/守卫）
2. 为NPC添加精灵动画
3. 通过pollinations.ai生成NPC图片
4. 优化网页控制器界面

## 重要提醒
- **不要修改核心移动逻辑**：`editor.cpp` 中的NPC移动代码已经稳定
- **API端口固定**：游戏API使用端口18080
- **网页控制器文件**：`web_controller_npc.html` 需要通过HTTP访问或使用`Image`对象
- **编译命令**：`cd build && mingw32-make -j8`
- **复制可执行文件**：`Copy-Item build\GameMakerCPlus.exe ..\`