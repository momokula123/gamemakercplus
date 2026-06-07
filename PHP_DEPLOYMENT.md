# PHP代理部署说明

## 文件说明

### 1. npc_proxy.php
PHP代理文件，用于转发请求到本地游戏服务器。

### 2. web_controller_npc.php
网页控制器PHP版本，通过PHP代理与游戏服务器通信。

## 部署步骤

### 1. 修改配置
编辑 `web_controller_npc.php`，修改第3行的代理地址：
```php
$proxyUrl = 'http://你的服务器IP/npc_proxy.php';
```

### 2. 上传文件
将以下文件上传到你的PHP服务器：
- `npc_proxy.php`
- `web_controller_npc.php`

### 3. 配置游戏服务器
确保游戏服务器允许来自PHP服务器的请求：
- 游戏服务器IP：PHP服务器IP
- 端口：18080

### 4. 访问网页控制器
局域网用户访问：
```
http://你的PHP服务器IP/web_controller_npc.php
```

## 工作流程

```
局域网用户浏览器
    ↓ HTTP请求
PHP服务器 (web_controller_npc.php)
    ↓ HTTP请求 (通过npc_proxy.php)
游戏服务器 (GameMakerCPlus.exe, 端口18080)
    ↓ 设置标志位
游戏主循环
    ↓ 移动NPC
```

## 注意事项

1. **CORS设置**：PHP代理已设置CORS头，允许跨域请求
2. **超时设置**：PHP代理超时设置为5秒
3. **错误处理**：PHP代理会返回错误信息
4. **安全性**：建议在生产环境中添加身份验证

## 测试

1. 访问PHP代理测试：
```
http://你的PHP服务器IP/npc_proxy.php?cmd=npc_list
```

2. 访问网页控制器：
```
http://你的PHP服务器IP/web_controller_npc.php
```

## 故障排除

### 问题1：无法连接游戏服务器
- 检查游戏服务器是否运行
- 检查防火墙设置
- 检查PHP服务器是否有权访问游戏服务器IP

### 问题2：CORS错误
- 确保PHP代理设置了正确的CORS头
- 检查浏览器控制台错误信息

### 问题3：请求超时
- 检查网络连接
- 增加PHP代理超时时间