# GameMaker C++ 编辑器开发经验总结

## 1. 路径与资源加载

### 核心原则
- **永远用 `GetModuleFileName` 获取 exe 目录作为基准路径**，不要依赖 CWD（当前工作目录）
- exe 可能在 `build/` 目录编译运行，assets 在项目根目录，相对路径无法对齐
- 搜索资源时试多个候选路径（`exeDir\..\assets\`、`exeDir\assets\`）

### 资源管理
- 所有美术资源（tiles、sprites）存入 SQLite DB，每次启动对比文件 mtime，有变化才更新
- **禁止任何形式缓存**：开发阶段每次从磁盘读原始文件，DB 只做索引和变更追踪
- `_thumb.png` 是缩略图缓存文件，不要用

## 2. SQLite 数据库架构

### 表结构
- `tiles`：id, name, category, width, height, png_data(BLOB), animated, frame_count, frame_width, frame_height, fps, file_path, file_mtime, created_at
- `sprites`：id, name, role, png_data(BLOB), frame_width, frame_height, anim_speed, pivot_x, pivot_y, collision_type, collision_data, created_at
- `tile_animations`：tile_id, frame_index, duration_ms, png_data(BLOB)
- `settings`：key, value
- `maps`：id, name, width, height, layer_data(BLOB), collision_data(BLOB), characters_data(BLOB), camera_x, camera_y, camera_zoom, render_order, layer_vis, layer_opa

### 约定
- DB 文件位置：`exeDir\editor.db`
- 资源加载：`importTileFromFile` 存 file_path + file_mtime
- 启动时：`getTileByPath` 检查是否存在，对比 mtime，`updateTileFromFile` 更新
- 显示：从磁盘 PNG 加载 bitmap（不从 DB blob 显示），DB 只做变更追踪

## 3. 地图存取

### 二进制格式 (.gmc)
- version 号递增，每次加新字段要 bump version
- layer_data 按层存储：layer_id(uint8) + data(WIDTH*HEIGHT bytes)
- collision_data：per-cell flags
- characters_data：Character 结构体序列化
- **新建地图时必须重新加载瓦片贴图到新 TileMap**
- loadMap 之后必须调用 tile 贴图初始化代码

### 关键 Bug 模式
- 新建地图后瓦片不见：因为新建 TileMap 不带 customTextures，需要重新 init 贴图
- **解决方案**：把瓦片贴图初始化抽成函数 `initTileTextures()`，新建和读取后都调用

## 4. 图层系统

### 固定图层（5层）
| Index | 名称 | 说明 |
|-------|------|------|
| 0 | 地形图片 | 地形视觉层 |
| 1 | 地形碰撞 | 碰撞标记层（半透明红色） |
| 2 | 角色装饰 | 角色/装饰/建筑 |
| 3 | 角色碰撞 | 碰撞胶囊（默认隐藏） |
| 4 | 前景层 | 渲染在角色上方，遮挡角色 |

### 渲染顺序
- `renderOrder[]` 数组控制渲染顺序，可在运行时调整
- `renderD2D()` 按 `renderOrder` 动态排序渲染，不再硬编码
- save/load 保存 `renderOrder`（version 6+）

## 5. 游戏运行窗口

### 关键规则
- **游戏镜头初始化以玩家为中心**，不要从编辑器镜头坐标换算
- 玩家初始位置在实体地形上时，先移到安全位置，`savedWorldX/Y` 同步更新
- 退出游戏后恢复玩家位置到保存值（不能是默认 0,0）
- gameTextures 和 gameCharSprites 在游戏结束时 Release + clear

## 6. UI 布局

### 固定常量
- `PANEL_WIDTH = 160`（左侧面板宽度）
- `TOP_BAR_H = 50`（顶部工具栏高度）
- `STATUS_BAR_H = 24`（底部状态栏高度）
- `LAYER_BTN_H = 18`（图层按钮高度）
- `TILE_START_Y = TOP_BAR_H + 26`（瓦片区起始 Y）
- `TILE_BTN_H = 44`, `TILE_BTN_GAP = 2`

### 布局原则
- **修改功能时不要动其他功能的布局**
- 瓦片面板位置固定在 `TILE_START_Y`，不要随意偏移
- 图层面板在顶部工具图标下方，横向按钮排列

## 7. 编码规范

### OOP 原则
- 所有资源、设定、碰撞、地图全部用 SQLite 面向对象管理
- LayerManager 管理图层数据，LayerPanel 管理 UI（如需要）
- AssetDatabase 统一管理所有资产的 CRUD

### 构建
- 编译命令：`cd build && mingw32-make -j8`
- 启动前 `taskkill /F /IM GameMakerCPlus.exe`
- **只启动一个实例**，不要重复 Start-Process

## 8. 禁止事项

1. **禁止** 使用 `_thumb.png` 缩略图文件
2. **禁止** 在开发阶段做资源缓存
3. **禁止** 改动功能时破坏其他已有功能的布局
4. **禁止** 重复启动多个程序窗口
5. **禁止** 用 CWD 相对路径加载资源
6. **禁止** 新建/读取地图后不重新初始化贴图
