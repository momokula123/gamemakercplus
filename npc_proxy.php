<?php
// NPC Controller PHP代理
// 用于将请求转发到本地游戏服务器

$gameServer = 'http://127.0.0.1:18080/api';

// 获取请求的cmd参数
$cmd = isset($_GET['cmd']) ? $_GET['cmd'] : '';

if (empty($cmd)) {
    // 返回帮助信息
    header('Content-Type: text/plain');
    echo "NPC Controller PHP Proxy\n";
    echo "Usage: ?cmd=npc_list|npc_select&id=N|npc_up|npc_down|npc_left|npc_right|npc_stop|pos|help\n";
    exit;
}

// 转发请求到游戏服务器
$url = $gameServer . '?cmd=' . urlencode($cmd);

// 使用cURL发送请求
$ch = curl_init();
curl_setopt($ch, CURLOPT_URL, $url);
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_setopt($ch, CURLOPT_TIMEOUT, 5);
curl_setopt($ch, CURLOPT_FOLLOWLOCATION, true);

$response = curl_exec($ch);
$httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
$error = curl_error($ch);
curl_close($ch);

// 设置响应头
header('Content-Type: text/plain');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

if ($error) {
    header('HTTP/1.1 500 Internal Server Error');
    echo "Error: " . $error;
} else {
    header('HTTP/1.1 ' . $httpCode . ' OK');
    echo $response;
}
?>