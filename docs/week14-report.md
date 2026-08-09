# 第 14 周实施报告：交互式编辑（FR-7）

**完成日期：2026-08-09**  
**范围：Vue + Qt/OpenGL/C++ + WebSocket 的植物结构交互编辑链路**

## 1. 交付摘要

本周完成从编辑输入到渲染结果同步的完整闭环：

```text
屏幕/面板编辑输入
  -> 射线拾取或节点选择
  -> Scale / Bend / Rotate / Parameter 工具
  -> PlantModel 更新与校验
  -> PBD、MetaballField、Marching Cubes 重建
  -> OpenGL 网格刷新
  -> WebSocket 权威状态确认
  -> Vue 编辑面板反馈
```

实现覆盖 FR-7 的节点级与整株级选择、变换编辑、参数编辑、撤销、重置、网格重建和前后端协议同步。

## 2. 已完成功能

| 子任务 | 实现 |
| --- | --- |
| 14-1 射线与拾取 | `RayPicker` 将屏幕坐标反投影为世界射线，支持节点、叶片球体求交，并选择最近命中对象；支持整株模式返回根节点。 |
| 14-2 选择与 Gizmo | `SelectionManager` 保存悬停/选中状态；`GizmoRenderer` 输出节点高亮、坐标轴和整株包围盒。 |
| 14-3 缩放 | `ScaleTool` 支持均匀和按轴缩放，对选中节点子树同步更新位置、方向、长度、半径与关联叶片；约束最小长度和半径。 |
| 14-4 弯曲 | `BendTool` 提供 Bézier 预览，并通过稳定的子树旋转保持父子拓扑及枝条长度。 |
| 14-5 旋转 | `RotateTool` 以四元数绕指定轴和枢轴旋转整个子树，同步更新枝条和叶片方向。 |
| 14-6 参数 | `NodeParameterEditor` 可编辑方位角、长度、半径、叶密度、年龄和生长层级，并执行范围校验。 |
| 14-7 重建 | 每次已接受编辑均校验模型、重标定生长基线、重建 PBD/Metaball/Marching Cubes，并发送表面与完整状态更新。预览网格间距为 `0.26`，提交网格间距为 `0.18`。 |
| 14-8 历史 | `EditHistory` 默认最多保存 50 个手势前快照；一次拖拽只形成一条撤销记录。支持取消预览、撤销与恢复初始植物。 |
| 14-9 协议与面板 | Qt WebSocket 服务端新增编辑指令；Vue `PlantEditPanel` 提供节点 ID、四类工具、预览、提交、撤销和重置操作。 |
| 14-10 回归 | 将 `PlantEditDemo` 纳入 CTest，并新增引擎级回归程序验证网格版本、编辑版本、撤销和重置。 |

## 3. WebSocket 编辑协议

所有入站编辑消息均要求 `protocolVersion: 1`，并按现有服务端结构化错误响应校验 `plantId`、`nodeId`、工具类型和数值有限性。

### 请求

- `edit.begin`：开始一次编辑手势。
- `edit.update`：执行 `scale`、`bend`、`rotate` 或 `parameter` 编辑；`preview: true` 用于拖拽预览。
- `edit.commit`：提交当前手势并生成一条撤销记录。
- `edit.undo`：撤销最近一次已提交编辑。
- `edit.reset`：恢复构造时的初始植物快照。

典型 `edit.update`：

```json
{
  "type": "edit.update",
  "protocolVersion": 1,
  "requestId": "edit-001",
  "plantId": 1,
  "nodeId": 12,
  "mode": "node",
  "tool": "scale",
  "preview": true,
  "params": {
    "scale": 1.2,
    "axis": "uniform",
    "scaleLeaves": true
  }
}
```

### 响应

已接受编辑通过 `plant.edit.updated` 返回权威版本信息：

```json
{
  "type": "plant.edit.updated",
  "protocolVersion": 1,
  "requestId": "edit-001",
  "plantId": 1,
  "nodeId": 12,
  "revision": 18,
  "meshVersion": 9,
  "rebuildCompleted": true,
  "canUndo": true
}
```

当前仓库运行的是 Qt5 WebSocket 服务端，因此协议直接调用 `SimulationEngine`，而不是额外引入未使用的 FastAPI 转发层；这样可以立即返回准确的 `revision` 与 `meshVersion`。

## 4. 前端交互

`frontend/src/components/PlantEditPanel.vue` 提供：

- 节点 ID 选择与当前骨架节点摘要；
- Scale、Bend、Rotate、Parameters 四种编辑模式；
- Start preview、Commit edit、Undo、Reset 控制；
- `requestAnimationFrame` 合并高频预览请求，确保每个浏览器绘制帧最多发出一次 `edit.update`；
- 服务端确认后显示编辑修订号、网格版本和可撤销状态。

## 5. 回归测试与修复

### CTest 项目

- `PlantEditRegression`：既有 `PlantEditDemo`，覆盖射线最近命中、整株拾取、Gizmo、缩放、弯曲、四元数旋转、参数更新、取消和撤销的数值结果。
- `PlantEditEngineRegression`：覆盖 `SimulationEngine` 的无效编辑拒绝、预览重建、提交、撤销、单次编辑与重置。

关键精度断言使用 `1e-4` 容差；JSON 快照恢复采用紧凑 JSON 的精确比较。

### 本周发现并修复

1. 无效的隐式编辑以前会调用公开 `cancelEdit()`，导致虽然编辑被拒绝但仍触发一次网格重建和修订号增长。现改为仅恢复内部历史快照，不生成用户可见更新。
2. `resetPlant()` 调用零年 `resetGrowth()` 时，`GrowthClock` 的同步 tick 会对刚恢复的初始快照再次应用幼苗缩放。现将该 tick 标记为恢复状态，保证零年重置恢复构造时的精确快照。

## 6. 验证命令

```powershell
cmake -S . -B build
MSBuild build\PlantSimulationSystem.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
MSBuild build\PlantEditEngineRegression.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
ctest --test-dir build -C Debug -R "PlantEdit(Regression|EngineRegression)" --output-on-failure

Push-Location frontend
npm test
npm run build
Pop-Location
```

验证结果：编辑 CTest 两项均通过；前端 Vitest 通过（2 个测试文件、9 项断言）；Vue TypeScript 生产构建通过。

## 7. 已知限制与后续工作

- 弯曲的提交实现采用稳定的子树刚体旋转；Bézier 曲线目前用于编辑预览，而非将每个中间骨架节点逐点投影到曲线。
- Vue 面板当前通过节点 ID 进行编辑；浏览器画布上的精确 GPU/射线拾取尚未接入前端交互层。
- 预览编辑每帧都会重建 CPU Marching Cubes 网格。后续可接入 GPU Compute Shader、节流和增量网格策略。
- 项目当前只有 Qt WebSocket 后端，没有实际运行的 FastAPI 服务；若将来拆分为独立服务，应保持本协议字段和版本校验不变。
- 尚未生成本周四种编辑操作的录屏或截图，可作为演示材料补充。

## 8. 本周提交

| 提交 | 内容 |
| --- | --- |
| `6e12c87` | 射线拾取与整株选择 |
| `5d65815` | 选择高亮与 Gizmo |
| `983ca57` | 子树缩放工具 |
| `a4ce95d` | 受约束枝条弯曲工具 |
| `4ed6f16` | 四元数子树旋转工具 |
| `4e1c28c` | 编辑历史、撤销和重置 |
| `61e9220` | WebSocket 编辑协议与 Vue 面板 |
| `d90c175` | 编辑 CTest 回归覆盖及重置/无效编辑修复 |
