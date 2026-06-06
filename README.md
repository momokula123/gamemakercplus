# GameMakerC++

基于 C++、Direct2D 和 GDI+ 构建的轻量级 2D 像素风地图编辑器与游戏运行时。

## 功能特性

- **瓦片地图编辑器** — 7 层系统（地形层、绘制层、碰撞层、角色层、前景层、前景绘制层、角色碰撞层）
- **Direct2D 硬件加速渲染** — 支持软件回退
- **笔刷工具** — 可调笔刷大小（1-10），绿色笔刷预览
- **网格瓦片导入** — 导入大图自动切割为瓦片网格
- **角色系统** — 支持精灵动画
- **地图序列化** — 保存/加载 .gmc 格式
- **游戏运行模式** — WASD 移动，ESC 退出
- **网页控制器** — 通过 HTTP API (端口 18080) 远程控制角色移动
- **暗色主题 UI** — 底部栏布局（虚幻引擎风格）

## 构建方法

需要 Windows + MinGW + CMake。

```bash
cd build
cmake .. -G "MinGW Makefiles"
mingw32-make -j8
```

## 依赖项

- Direct2D / DirectWrite（Windows SDK）
- GDI+（Windows）
- SQLite3（已内置）
- stb_image（已内置）

## 许可证

MIT
