<?php
// NPC Controller PHP代理（使用socket）
// 用于将请求转发到本地游戏服务器

// 修改为你的游戏服务器公网地址
$gameServer = '43.240.220.142';
$gamePort = 18080;

// 获取请求的cmd参数
$cmd = isset($_GET['cmd']) ? $_GET['cmd'] : '';

if (empty($cmd)) {
    // 返回帮助信息
    header('Content-Type: text/plain');
    echo "NPC Controller PHP Proxy\n";
    echo "Usage: ?cmd=npc_list|npc_select&id=N|npc_up|npc_down|npc_left|npc_right|npc_stop|pos|help\n";
    exit;
}

// 构建HTTP请求
$request = "GET /api?cmd=" . urlencode($cmd) . " HTTP/1.1\r\n";
$request .= "Host: " . $gameServer . ":" . $gamePort . "\r\n";
$request .= "Connection: close\r\n";
$request .= "\r\n";

// 创建socket连接
$socket = @socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
if ($socket === false) {
    header('Content-Type: text/plain');
    header('HTTP/1.1 500 Internal Server Error');
    echo "Error: Failed to create socket";
    exit;
}

// 连接到游戏服务器
$result = @socket_connect($socket, $gameServer, $gamePort);
if ($result === false) {
    header('Content-Type: text/plain');
    header('HTTP/1.1 500 Internal Server Error');
    echo "Error: Failed to connect to game server at " . $gameServer . ":" . $gamePort;
    socket_close($socket);
    exit;
}

// 发送请求
socket_write($socket, $request, strlen($request));

// 读取响应
$response = '';
while ($chunk = socket_read($socket, 1024)) {
    $response .= $chunk;
}

// 关闭连接
socket_close($socket);

// 提取响应体（去掉HTTP头）
$parts = explode("\r\n\r\n", $response, 2);
$body = isset($parts[1]) ? $parts[1] : $response;

// 设置响应头
header('Content-Type: text/plain');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

if (empty($body)) {
    header('HTTP/1.1 500 Internal Server Error');
    echo "Error: Empty response from game server";
} else {
    header('HTTP/1.1 200 OK');
    echo $body;
}
?>