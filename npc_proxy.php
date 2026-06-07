<?php
// NPC Controller PHP代理（使用socket）
// 用于将请求转发到本地游戏服务器
// 部署时修改下面两个变量为你的游戏服务器地址

 = 'YOUR_GAME_SERVER_IP';  // 替换为游戏服务器IP
 = 18080;                     // 游戏服务器端口

// 获取请求的cmd参数
 = isset(['cmd']) ? ['cmd'] : '';

if (empty()) {
    header('Content-Type: text/plain');
    echo "NPC Controller PHP Proxy\n";
    echo "Usage: ?cmd=npc_list|npc_select&id=N|npc_up|npc_down|npc_left|npc_right|npc_stop|pos|help\n";
    exit;
}

// 构建HTTP请求
 = "GET /api?cmd=" . urlencode() . " HTTP/1.1\r\n";
 .= "Host: " .  . ":" .  . "\r\n";
 .= "Connection: close\r\n";
 .= "\r\n";

// 创建socket连接
 = @socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
if ( === false) {
    header('Content-Type: text/plain');
    header('HTTP/1.1 500 Internal Server Error');
    echo "Error: Failed to create socket";
    exit;
}

 = @socket_connect(, , );
if ( === false) {
    header('Content-Type: text/plain');
    header('HTTP/1.1 500 Internal Server Error');
    echo "Error: Failed to connect to game server at " .  . ":" . ;
    socket_close();
    exit;
}

socket_write(, , strlen());

 = '';
while ( = socket_read(, 1024)) {
     .= ;
}

socket_close();

 = explode("\r\n\r\n", , 2);
 = isset([1]) ? [1] : ;

header('Content-Type: text/plain');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

if (empty()) {
    header('HTTP/1.1 500 Internal Server Error');
    echo "Error: Empty response from game server";
} else {
    header('HTTP/1.1 200 OK');
    echo ;
}
?>
