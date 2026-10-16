# E-mote 插件剩余工作计划

<a id="execution-ledger"></a>

## 全计划执行清单 / 验收账本（2026-10-16，当前时点）

**目标仍是执行 E1–E17 全计划，不是整理文档或把 unknown 改成 done；计划未全部完成。**
本节是当前执行口径，只记当前时点的代码/测试/证据状态；下方历史记录保留原时点，其“完成”不自动延伸到当前源码或官方验收。
`[x]` 只确认写明的代码/数据归档/决策子项；依赖官方、真实样本或设备的验收仍为 `[ ]`。

### 验证时点（本机合成与构建验证）

- `build-test` ctest **27/27**；`build-cgl` ctest **29/29**（`compositor_regressions` 连续 3 次通过）。
- `build-mac`、`build-android`、`build-consumer`、`build-consumer-android` 构建通过；
  `tools/check_android_exports.py` 的 Android **12 个入口**导出检查通过；`git diff --check` 通过。
- 边界：以上是本机合成回归与构建/导出/链接验证，**不代表真实游戏、设备或官方一致**；
  Android 交叉产物不能在 Mac 运行，导出/链接成功不等于 JNI 行为兼容。更早的 22/22、23/23、
  24/24、25/25 等是此前时点，不与当前混用。

### 验收账本

- [x] E1 槽表归档与独立字节校验：120 行逐槽表 / 240 DWORD 校验；mirror 三个函数地址已核对为“入口 vs 内部证据范围”粒度不同、非冲突；完整 ABI 未知保留。
- [x] E4/E11 分数灰度、tint、模型镜像位已有代码；`build-cgl` 29/29（含 compositor 回归）覆盖离屏 CGL 像素断言，窗口/设备观感未补。
- [x] E5 决策 A：`setOuterForce`/`setOuterRot` 专用校验 + unsupported 代理已实现；不模拟物理。
- [x] E6 LZ4 校验加固、E10 多 blink 时钟修复、E14 隔离采集工具与 Mac 读档复现路径、E15 TagSync 只读查询（Lua 不注册）已落入当前源码。
- [x] E10 逐键表、E12 不暴露 Lua、E13 可选不启动、E17 按需延期的决策边界已记录。
- [ ] E2/E3：ordered pair、mirror 三态、flags/Step/Skip/Pass、独立 queuing 已实现并有合成回归；官方模型/轨迹对照缺，`PlayingTimelineFlagsAt` 与官方 `|1` 的差异未改。
- [ ] E6/E7/E8：真实 v2 body 加密样本、拆分模型及移动贴图样本验证；不由合成或字符串代证。
- [ ] E9/E16：Z 通道、完整 affine + shape leaf、warped mask source、meshSync、meshCombinator core 与同域 meshCombine 已接入；低位 sync / cc / 屏障 anchor 补偿 / 异域等为降级加载，曲线/模板/粒子及官方逐像素对照未完成。
- [ ] E10/E15：transition/immediate/profile/orbit 已接入；clamp / stereo / eye edge / eyebrow nodes / mouth shape 未实现；E15 真实等待/同步流程未验。
- [ ] E14：原矩阵其余场景、立绘/对话流畅性、官方同状态 A/B、0138 与实际 Android/JNI 合同。
- [ ] E17：取得目标需求并验证表情/表达变量、timeline、blend 的存读档恢复，或记录限定对象的明确无需求依据。
- [ ] 宿主与发布：Android 宿主合同（官方壳与 Tyranor-Next 各自 Activity/插件）验收；插件重新构建发布（TyranorNext 内 `libartemis-clean.so` 仍是旧 v29，需更新 manifest `pluginVersion`）。
- [ ] 最后一轮并行改动稳定后，重新完成主/detail 原 DoD 与宿主验收；不得复用旧测试时点冒充。

**标签含义**：`implemented`＝限定代码已落地；`synthetic`＝合成断言/运行证据（须区分已运行与仅有用例）；
`real`＝有边界的真实运行观察，不等于官方一致；`staticdecision`＝静态事实或产品决策，
不是动态通过；`missing`＝保留的验收缺口。各列可以同时成立，不存在“一列有证据即整项完成”。

| E | implemented | synthetic | real | staticdecision | missing / 下一步（保留原验收要求） |
|---|---|---|---|---|---|
| E1 | 槽表/差异归档，非 DLL bridge | 120 行逐槽表 + 240 DWORD 独立校验通过 | 无动态 ABI 验证 | player 95（含析构）+ device 25；18 个记录名称；mirror 三函数入口 vs 内部范围已核对 | 完整签名/多数名称/宿主映射未知；逐差异回归或不实现结论仍需跟进 |
| E2 | ordered pair 控制器（second 聚合、getter 分离、有限性）；mirror 三态（有效 = metadata XOR 用户，`Mirror` getter 返回用户状态）与 `variableMatchList` 字节子串取负 | emote_regressions / emote_mirror_regressions 随 27/27 通过 | 缺匹配变量真实模型对照 | ordered pair、variableMatchList 消费、mirror 三态为官方静态事实；`setMirror` 是项目扩展（官方样本 Lua 无注册） | 反序/共用 label/ease 与最终混合的官方对照；mirror 与 eye/selector/blink 覆盖顺序为本地策略未证；stereovision 未做 |
| E3 | flags bit0=0 清全部、bit1 私有差分（非 sequential）；Step/Skip/Pass 按官方静态语义；独立 `setQueuing`/`getQueuing` | emote_timeline_regressions 随 27/27 通过 | 缺官方/插件同调用轨迹 | bit1 非 sequential、Step 不调用 Progress、Pass 20 帧淡出已证 | `PlayingTimelineFlagsAt` 仍返回 stored flags（官方 getter 为 `|1`，未改，属已知差异）；保留三处结论与真实轨迹要求 |
| E4 | float 灰度与像素路径 | build-cgl 29/29（compositor 回归连续 3 次）含分数灰度/tint 断言 | 当前窗口/设备观感未补 | 无新增等价结论 | 保留 ANGLE 路径及必要观感复核 |
| E5 | 专用 `setOuterForce`/`setOuterRot` 校验后返回 false+reason；hair/bust 仅记录 | runtime 定向通过 | 无物理逐帧对照 | 选 A，明确不模拟；startWind 不被当成已实现模拟 | 保留决策依据；若转 B/C 需真实模型逐帧截图，不扩写未知物理单位/公式 |
| E6 | seed/v2 body、MDF/LZ4；LZ4 header/block/content 校验与边界加固 | psb_encrypted_regressions 合成坏流/校验用例 | 缺真实 v2 body 样本 | FilterTexture 为纹理 callback 路径，不是整包输出已证 | 真实加密模型加载；结构过滤范围与坏 seed/v2 错误歧义保留 |
| E7 | 单文件路径；多文件明确拒绝 | 无拆分一致性夹具 | 缺拆分样本 | 合并/附加查找均为候选方案 | 取得样本，定义重编号/冲突/原子失败，拆分与合并模型渲染一致 |
| E8 | 桌面格式/图集/mip level 0 | 现有合成块/裁切断言 | 移动格式（ETC1/PVRTC）无样本 | ETC1/PVRTC 继续拒绝 | 补移动样本及色序/alpha/裁切官方像素对照，不将无样本当全部不需要 |
| E9 | 有效 Z 通道（去 label hacks）；完整 affine + shape leaf；warped mask source；meshSync 有限作用域；meshCombinator core + 预算（总 64 MiB / 单 8 MiB / 4096 条）+ scene 接入；同域 meshCombine（combinator 优先于 bp）；降级加载并记录 UnsupportedFeatures（低位 sync、cc、屏障 anchor 补偿、异域等不应用）；图集解码缓存；stage 蒙版缓存按 owner + 解析源集合 | compositor_regressions（含双 owner、同标签左右源）、combinator scene 回归随 29/29 / 27/27 通过 | 真实 132 个 fg 模型 DecodePsb / Model / Player Load 全部成功（仅加载级）；每个模型命中 clampControl.type0/1、meshSync.lowChannels、meshCombine.fallback，131 个有 timeline.openEnded；`EmoteScene::Load` 约 4.2s→约 50ms；Mac 读档 say_6/shi_36 眼睛虹膜/高光/红晕恢复（对比图不进仓库） | Z 有限静态 review；屏障 anchor 补偿尚未恢复；ancestor meshCombine 研究 | 曲线/模板/粒子、降级项的官方对照、最终 GL/官方同状态逐像素验收；不关闭 E9 |
| E10 | transition / immediate / profile / orbit 已接入 model/player（`emote_control.*`）；mirror 三态已对齐；blink/selector/attrcomp 子集；多 blink 时钟修复 | emote_control_regressions / emote_control_player_regressions / emote_mirror_regressions 随 27/27 通过 | 自然眨眼/选项及新增控制器官方动态对照缺；真实游戏快照中未观察到眨眼（不判定存在与否） | transition/immediate/clamp/eyebrow/mouth/orbit/profile/精确 face 默认名已 official-runtime-static-confirmed，project-support-separate | clamp（type0 未知 / type1 圆投影）、stereo、eye edge、eyebrow nodes、mouth shape 未实现；按能力表补算法/实现；不能排期＝动态兼容 |
| E11 | MODULATE2X tint、metadata.mirror | build-cgl 29/29 数值/GL 像素断言 | 当前镜像/颜色官方观感未补 | mirror 与 mirrorControl 分开 | RGB/alpha 与目标模型朝向复核 |
| E12 | 自有 EmoteScene 求值，不公开 native 结构 | 无 native ABI 用例 | 无新增公开接口需求实证 | 保留不暴露 Lua 决策 | native 输出结构/签名未知；若有调用先补参数/返回使用证据，非结构等价结项 |
| E13 | 不启动可选动态工具（无 Windows 运行环境） | 无真实轨迹 diff | Windows/D3D9 参照环境缺 | 本轮选择不启动，可选决策不是 diff 通过 | 若启动仍需真实会话轨迹无未解释差异及工具纳入 tools；不阻塞可独立项 |
| E14 | save-dir/frames/timeout/completion 工具与精确 HIDE/probe；Mac 读档复现路径（见下） | 工具 selftests（`tools/test_emote_validation.py`） | 独立 savedir host 标题 1200 帧/exit0 无立绘；Mac 读档复现 say_6/shi_36 双立绘；newgame 2600 被 name modal 超时 | phone 官方状态待用户准备；不据此阻塞整个 goal | 原矩阵全项/立绘与流畅对话/官方同状态逐像素对照（手机，进行中）、0138、性能与设备/JNI；超时不算流畅通过 |
| E15 | TagSync 只读查询（`EmoteModel::NextMotionSyncFrame`），Lua 不注册；旧 `SyncFrame`/C++ `SkipToSync` 为项目 legacy 扩展 | emote_sync_regressions 随 27/27 通过 | 真实等待/同步流程未验 | Android Lua 无 skipToSync（官方样本无注册）；native 非 loop 查 tag(type1,time>=current,content.sync)，fallback lastTime，无 selfSyncTime；eventwait 与 IsAnimating 分离 | 真实对话不挂起/不提前；旧三来源条文保留但前提被推翻，不再照搬实现 |
| E16 | 几何命中（quad / mesh / shape）+ 三角形容差修复；stage full-affine / shape leaf；`contains`/`hitTest` 为项目扩展 | 合成定向通过；非官方双 API 等价 | 真实点击/遮挡/坐标官方对照缺 | 官方 Lua 只有 `isAnimating`，无 contains/hitTest 注册；native Contains 为 shape type1/geometry | 点边顶点/仿射边界；官方 native 几何/坐标与项目扩展分别验收，不假称官方双 API 差别已实现 |
| E17 | ARCV/onLoad 重建安全网，不含 player 精确状态序列化 | 现有存档 callback 断言不覆盖 emote 恢复一致 | 需求及表情/timeline/blend 恢复证据缺 | 按需延期，不声称所有游戏不需要 | 按需求验证表达变量/timeline/blend、进度/首帧；原“官方一致或明确不需求”门槛不删 |

### 当前补充（与上文并存，不删任何验收条文）

- 格式与行为证据见 [emote-format-evidence.md](emote-format-evidence.md)：有效 Z 取累计
  `coord[2]`、`metadata.zorder` 仅静态交叉校验；rawMeshList 为 4x4 float32LE、每 mesh
  128 字节、首条绝对后续 delta、`neutralIndex=-1` 合法仅作标志；meshCombine 父链停止规则；
  combinator 优先于 frame `bp`，全 neutral 也不回退；低位 sync 与 shape 位区分。
  **屏障 anchor 补偿未恢复**（项目降级加载并记录，不输出猜测值），E9 不关闭。
- **mirror**：有效镜像 = metadata XOR 用户状态；`Mirror` getter 返回用户状态；mirrorControl
  匹配为大小写敏感字节子串，空项匹配全部；有效镜像且匹配时对整个合成值取负，getter 不变。
- **orbit**：首次 Progress 不消费 dt；整周期回同 index 不重发；tween
  duration = `(index+1)*(T/N) - phase`。
- **降级加载**：不支持的特征（`clampControl.type0/1`、`meshSync.lowChannels`、`mesh.cc`、
  `meshCombine.fallback.*`、`timeline.openEnded`）不再拒绝整个模型，而是不应用并经
  `EmoteUnsupportedFeatures` 记录首见日志；这不是已支持。
- **stencil 蒙版缓存缺陷已修**：原按标签列表哈希分配/缓存 stage 蒙版纹理，导致 (1) 同标签的
  左右被蒙版部件（如两只眼）共用首个部件解析出的最近源；(2) 两个 owner 的同标签集共用一张
  纹理。现按“owner + 实际解析出的源集合”分配/缓存。回归：`compositor_regressions`（CGL）双 owner、
  同标签左右源用例。官方同状态逐像素验收仍无。
- **E14 Mac 读档复现路径**（`artemis-mac <游戏目录> --os android --save-dir <隔离目录> --tap … --snapshot …`）：
  标题约第 1500 帧才可交互；点击序列（stage 像素@帧号）`1684,293@1560`、`390,260@1700`、
  `806,497@1780`，对应标题→读取→槽位→确认。必须用隔离存档目录，不碰真实存档；
  它只隔离常规存档，不是脚本沙箱。
- 诊断：`ARTC_EMOTE_DUMP` 记录 player 命令与合成状态，供 `tools/emote_preview` 回放；
  仅为开发诊断，不是官方轨迹。

### 仍缺（保留原验收要求）

1. 官方内核同状态逐像素对照（手机，进行中）；0138 闭环。
2. Android 宿主合同验收：官方壳与 Tyranor-Next 各自的 Activity/插件分别验证；
   导出/链接/构建通过不等于 JNI 行为兼容（见 `docs/embedding.md`）。
3. 插件重新构建与发布：TyranorNext 内 `libartemis-clean.so` 仍是旧 v29，需更新 manifest `pluginVersion`。
4. E6 真实 v2 body 样本、E7 拆分样本、E8 移动端压缩格式样本；E13 不启动（无 Windows 运行环境）；
   E17 需求门槛；E10 未实现控制器；E9 降级项。

> **定位**：E-mote 兼容面（PSB 模型解码 → 场景求值 → `EmotePlayer` 播放 →
> Lua `e:createEmoteLayer` 代理）的未完成项清单。按优先级排列，每项独立
> 可立项，不阻塞其余工作。
>
> **依据**：对官方驱动 `emotedriver.dll`（PE32 x86，2018-12-21，M2 E-mote 运行时
> + 记录/回放驱动）的静态分析；历史计数混用了析构口径，现以 driver-contract 的
> player 95 / device 25（均含析构）及 120 行 CSV 为准。18 条 record/replay 格式串
> 支持名称与记录参数形状，**不恢复完整 ABI 或 native 时间单位**。
> 旧临时分析见附录 C；持久证据见 `docs/emote-driver-contract.md` 与槽表。
>
> **维护约定**：改动 E-mote 子系统必须先扩 `tests/emote_regressions.cpp` 或
> `tests/runtime_regressions.cpp`（AGENT.md §8）；物理/网格等无法渲染的行为
> 保持显式不实现，不做"假装"。

## 历史基线（已落地子集，勿重复实现；非当前全部验收）

| 子系统 | 状态 |
|---|---|
| PSB/MDF | v2–v4 解码、zlib MDF、名称/资源/数组；**加密头部**（密钥推导 + adler32 校验，`src/pack/psb.cpp:147-189`） |
| 贴图 | RGBA8、CI8/调色板、DXT5、BC7；`none`/`RL` 压缩；桌面 A8R8G8B8 通道交换 |
| 场景 | 图层/关键帧求值、`parameterize` 变量绑定、子动作、per-icon 贝塞尔网格（`bp`）、按 ID 的 z 序 |
| 播放器 | 时间轴播放/停止/顺序队列、hold-end、差分（diff）时间轴、变量、坐标/缩放/旋转/颜色/透明度、镜像、显隐、fade in/out、**blend ratio 过渡**、Skip/Pass/Step、**灰度**、**差分变量** |
| Lua 代理 | `createEmoteLayer`/`getEmoteLayer`/`getEmoteVersion`；set*/get* 全套；`setScale`（3 参官方形式 + 4 参扩展）、fade/blendRatio 已按驱动契约带 ease/flags |
| 回归 | `emote_regressions`（模型/播放器/PSB）、`emote_mesh_regressions`、`emote_timeline_regressions`、`runtime_regressions`（Lua 全链路） |

---

## 总览

| 编号 | 任务 | 优先级 | 依赖 | 主要依据 |
|---|---|---|---|---|
| E1 | 官方 `IEmotePlayer` 94 槽契约逐项归档 | P0 | 无 | 虚表 + 反汇编（仅 18 槽有名字） |
| E2 | `SetVariableDiff`/`GetVariableDiff` 语义核对 | P0 | 真机/真实模型 | 驱动 pair 控制、`variableMatchList` |
| E3 | `PlayTimeline` flags / `Pass`/`Step` 语义核对 | P0 | 真机 | 驱动 `0x10019960`、`0x10018dd0` |
| E4 | 灰度 GL 观感验证 + 分数灰度回归 | P0 | GL 环境 | `layer_shader.cpp` 已改 float |
| E5 | 物理（`SetOuterForce`/`SetOuterRot`/`startWind`）决策与实现 | P1 | E1、真实模型 | `bustControl` 等元数据 |
| E6 | 加密 PSB 整包（`EmoteFilterTexture`） | P1 | 样例文件 | 导出 `0x10003920` |
| E7 | 多文件（拆分）PSB 归档 | P1 | 样例文件 | `createEmoteLayer` 显式拒绝 |
| E8 | 贴图格式补齐（DXT1/3、16bit、ETC1/PVRTC…） | P2 | E6 | 驱动格式串 |
| E9 | 场景节点：模板/遮罩合成、网格扩展、粒子 | P2 | 真实模型 | 加载期显式拒绝清单 |
| E10 | 控制类元数据（眨眼/口型/选择器/orbit…） | P1 | 真实模型 + krkrsdl3 对照 | `*Control` 元数据键 |
| E11 | `SetColor` RGB tint（colormultiply）/ 模型镜像位 | P2 | 无 | 驱动 `SetColor %d %f %f`、`metadata.mirror` |
| E12 | `CalcLayerFrameInfo` 是否需要暴露 | P2 | E1 | `0x1000b370` |
| E13 | 驱动 record/replay 用作差分验证工具 | P3 | Windows 参照环境 | `0x1001ae50` 命令解析 |
| E14 | 实测游戏与 GL 冒烟矩阵（持续） | P0 | 真机 | AGENT.md §9 |
| E15 | 同步点与动画结束语义（`syncTime`/`skipToSync`） | P1 | 真实模型 | krkrsdl3 `GenerateAniTree` |
| E16 | 命中测试（`contains`/`hitTest`、shape/blank 源） | P2 | 真实游戏调用 | krkrsdl3 `EmoteHitFrame` |
| E17 | 播放状态序列化（`serialize`/`unserialize`） | P3（按需） | 存档系统 | krkrsdl3 `EmotePlayer` |

> 外部参考：`/Users/weiss/github- engine/krkrsdl3/plugins/emoteplayer`（KiriKiri Z，
> BSD 三条款式）。只提取行为/接口事实，不复制代码。对照明细见
> 「附录 D — krkrsdl3 对照」。E10/E15 的依据来自该实现。
>
> 可执行细化（WP0–WP11：契约对齐/眨眼/选择器/attrcomp/镜像/PSB/图集/曲线/命中测试…）
> 见 [`docs/emote-detail-plan.md`](emote-detail-plan.md)。
>
> **契约修订（WP0，真实游戏脚本证据）**：Lua 层 transition/fade/progress 单位是
> **帧**（60fps，`ex.frametime=16.666`）而非毫秒；`setScale(scale, origin_x,
> origin_y)`、`setCoord(x, y, z, angle)`；`getEmoteLayer` 接受表
> `{id=…, next=…}`；甜蜜女友3 还调用 `setMeshDivisionRatio/setHairScale/
> setBustScale`。落地前先做 WP0。

## 历史落地状态快照（保留，当前以顶部账本为准）

> 下表是此前记录，不作为当前结项清单。其中 E1“完成”仅限归档，E6/E10/E16
> 的整体完成表述撤回；E12“游戏脚本无调用”不作全称结论。其余需真实验收的条件
> 均保留，当前细分状态见顶部；旧测试时点不得移用于新修改。

| 项 | 状态 |
|---|---|
| E1 | **完成**：`docs/emote-driver-contract.md`（18 项命名方法 + 导出/容器/未恢复清单） |
| E2 | 保持"等值反向"推断；未观察到真实调用，待含匹配变量的样本 |
| E3 | Lua 面按脚本证据对齐（WP0）；驱动 flags bit0 清空语义与 Pass/Step 差异仍待真机复核 |
| E4 | **完成**：新增 `ARTC_TEST_CGL`（macOS 离屏 CGL，无需 ANGLE/窗口）跑通 `compositor_regressions`，含分数灰度与 MODULATE2X `colormultiply` 像素断言 |
| E5 | **决策 A**：物理显式不支持；`setHairScale/setBustScale` 已注册并记录 |
| E6 | **完成**：v2 body 解密、宿主 seed 钩子、`lzfs` LZ4 帧；整包 `EmoteFilterTexture` 固定 key 路径按需 |
| E7 | 维持单文件（参考宿主同样要求 exactly one）；无拆分样本 |
| E8 | **完成（桌面格式）**：图集裁切 + DXT1/DXT3/16bit（4444/5551/5650）/A8L8/RGBX8 + mip 链容错，均有合成回归；ETC1/PVRTC 无命中样本，维持显式拒绝 |
| E9 | **部分落地，待补视觉验证**：per-frame `color`、blank 描述符、`priority`、`bm` 混合、stencil/mask 合成、HOLD 语义与 1ms 时间容差；meshSync 已实现有限形状作用域（早期“无载荷结项”已撤回），待更充分回归；owner 层序返修与独立复审通过，P1 在限定合成/静态边界可关闭，粒子无样本。见 §E9 本轮审查状态 |
| E10 | **完成**：眨眼/选择器/attrcomp；clampControl/talkLabel 等待证据 |
| E11 | **完成**：MODULATE2X 颜色 tint + 模型镜像位 |
| E12 | **决策**：由 EmoteScene 内部覆盖（游戏脚本无调用），Lua 不暴露 |
| E13 | 推迟（需 Windows 参照运行 + 工具） |
| E14 | **部分完成（历史验证）**：host 冒烟通过（常轨脱离 300 帧、甜蜜女友3 prologue 1200 帧，立绘模型加载成功；仅遗留缺片 movie 告警）；**TyranorNext 实机**（`libartemis-clean.so`，甜蜜女友3 存档点）曾验证立绘渲染，并据实机修复 inheritMask/bm/盒中心偏移/`Animated::Finish` 归零/HOLD 子树等项；当时缺失的 stencil mask 后续已实现，但层序及官方精确 A/B 仍未闭环，本轮测试边界见 §E9 |
| E15 | **部分**：sync 推导 + C++ `SkipToSync` 完成；Lua 注册待真机证据 |
| E16 | **完成**：`contains`/`hitTest` + shape/blank/clip 源 |
| E17 | 按需（未观察到写档调用），暂不实现 |

---

## E1 — 官方 `IEmotePlayer` 契约逐项归档（P0）

**当前更正**：`emote-driver-slots.csv` 120 行（player 95 + device 25，含析构）
与 240 源 DWORD 的独立验证已通过。历史 94/25 是混合计数口径；完整 C++ 签名、
多数名称与宿主调用映射仍未知，不能称 ABI 恢复完成。原验收的逐差异处理要求保留。

### 问题与证据

- 驱动 `PEmotePlayer` 虚表（`0x1008ba88`，95 槽）中只有 18 个方法有
  record/replay 名字（附录 A），其余槽位（getter、层信息、命中测试、
  `MMotionPlayer` 转发等）尚未命名。
- 项目 Lua 代理是按"游戏脚本实际调用面"反推的，可能与官方槽位存在遗漏
  或语义错位（如 flags、时序）。

### 方案

1. 用 r2 对每个槽位函数做行为分类（getter/setter/转发/复杂逻辑），
   结合被调 trace 字符串、`axt` 调用图与字段偏移，给出槽位→候选名映射。
2. 汇总为 `docs/emote-driver-contract.md`：槽位号、字节偏移、函数地址、
   参数、返回值、语义与证据。
3. 与现有 Lua 方法表逐项 diff，产出"缺失/差异"清单，作为 E2–E12 的输入。

### 验收标准

- 契约文档覆盖全部 94/25 槽，未知槽显式标注"未命名/存疑"。
- 清单中每个差异项有对应回归或明确的"不实现"结论。

---

## E2 — `SetVariableDiff` / `GetVariableDiff` 语义核对（P0）

**当前实现**：普通 ordered-pair 控制（second 聚合、getter 分离、有限性）与 getter 已做；
mirror 三态（有效 = metadata XOR 用户状态）及 `variableMatchList` 匹配后对合成值取负已实现，
`setMirror` 为项目扩展。mirror 与 eye/selector/blink 的覆盖顺序为本地策略未证，stereovision 未做，
真实匹配模型对照缺，不代表真实模型兼容。下方原样本、ease/未知变量/getter 验收保留。

### 问题与证据

- **撤回旧“仅第一个 label 为 key”结论**：驱动 key 是有序二元组 `(label,pair)`，
  单值控制器及 pair getter 路径见 driver-contract §4；getter 正式名称未恢复。
  `variableMatchList` 已有解析及参与字符串匹配的静态证据，但不证明它是 pair 白名单。
- 历史曾按等值反向（label=+v、pair=-v）直接写两变量且未暴露 getter；当前
  `SetVariableDiff/GetVariableDiff` 已改普通 ordered-pair 控制，旧 +/- 不能再代表现实现。
  特殊 mirror、最终组合及真实匹配模型验证仍保留未达项。

### 方案

1. 找含匹配变量的真实模型（`variableMatchList` 非空），观察官方驱动/游戏
   对同一调用的轨迹（可用 E13 的记录文件）。
2. 必要时修正符号/组合方式；先确认 `variableMatchList` 的完整运行时用途，再决定
   是否用于 pair 约束（旧“白名单”仅为方案假设，不能作为官方事实实施）。当前项目
   要求两 label 都是模型变量；最终混合、逆序 pair、多 pair 共用 label 继续调查。
3. 已有 getter 后继续补读回/反序/共用 label 与失败路径回归；真实模型验证不因 API 存在而省略。

### 验收标准

- 真实模型上 `setVariableDiff` 后渲染与官方一致（截图/逐帧对比）。
- 回归覆盖：符号、过渡 ease、未知变量报错、`getVariableDiff` 读回。

---

## E3 — `PlayTimeline` flags / `Pass` / `Step` 语义核对（P0）

### 问题与证据

- 驱动静态已纠正：`PlayTimeline` bit0=0 调 `StopTimeline("")` 清**全部**时间轴，
  bit0=1 保留；bit1 为逐层控制重置（`0x10018dd0`），不是 sequential；见 driver-contract §5。
- 历史项目把 bit1 当顺序队列、Pass 当立即完成过渡、Step 当固定推进一帧；这些旧规则
  不能继续当官方语义。新静态事实确认 **bit1 非 sequential、Step 不调用 Progress**；
  当前源码已按此实现：flags bit0=0 清全部、bit1 私有差分（非 sequential）、Step/Skip/Pass
  按官方静态语义、独立 `setQueuing`/`getQueuing`，有合成回归（emote_timeline_regressions）。
  native Pass 有 `(0,20.0,0,1)` 淡出，还遍历变量关键帧/维护列表，不能简化成仅 20ms；
  单位及插件映射仍独立核验。**已知差异**：`PlayingTimelineFlagsAt` 仍返回 stored flags
  （官方 getter 为 `|1`，未改）。官方/插件同调用轨迹及真实验收仍缺。
- 这三处是"引擎插件语义 vs 驱动语义"的分层问题，需确认项目按哪一层对齐。

### 方案

1. 在实测游戏上开 `tag[trace]`/emote 日志，记录实际调用序列与视觉结果。
2. 若发现走样，优先按"Lua 可见行为"对齐，并在 `emote_player.h` 注释中
   记录与驱动的差异；不得静默混用两套 flags。

### 验收标准

- 三处语义各有结论（保持/修改）与至少一条回归或真机记录。

---

## E4 — 灰度 GL 观感验证 + 分数灰度回归（P0）

### 问题与证据

- `LayerEffect::grayscale` 已由 bool 改为 0..1 float，着色器本来就是
  `uniform float`；现有 GL 回归只测了 `grayscale=1`（`compositor_regressions.cpp:541`）。
- `emote_regressions` 只在桩合成器上验证了 prop 下发，未验证像素结果。

### 方案（已落地）

1. `compositor_regressions.cpp` 新增分数灰度用例（`grayscale=0.5` →
   `mix(luma,c,0.5)`）与 `colormultiply=0x808080`（MODULATE2X 中性 0x80）
   的像素断言，归属 GL 回归。
2. 新增 `ARTC_TEST_CGL` 构建开关：macOS 用离屏 CGL 上下文跑同一份
   `compositor_regressions`，无需 ANGLE 或窗口。无 drawable 的 CGL 默认
   帧缓冲不可用，测试把 "屏幕"（含 `LayerShaders::Begin/End` 的
   `parent`）指向自建 FBO，等价 EGL pbuffer。
3. 真机/窗口观感仍建议在 mac 宿主复看。

### 验收标准

- `ARTC_TEST_CGL=ON` 的 `compositor_regressions` 通过（本机 Apple M1 Pro
  已过）；`ARTC_TEST_GLES` 的 ANGLE 路径保持不变。

---

## E5 — 物理：`SetOuterForce` / `SetOuterRot` / `startWind`（P1）

### 问题与证据

- 驱动：`SetOuterForce %s %f %f %f %f`（part = `bust`/`hair`/`parts`）、
  `SetOuterRot %f %f %f`；模型元数据含 `bustControl`/`hairControl`/
  `partsControl` 与 `gravity`/`spring`/`friction`/`bendR`/`bendS`/`b_rate`/
  `v_bound`/`ud_eft`/`bend_spd`/`bend_vol`/`length`/`var_lr`/`var_ud`/`var_lrm`。
- 历史仅有 logging stub；当前已注册专用 `m_setOuterForce/m_setOuterRot`，校验参数、
  优先解析失效句柄后返回 `false, reason`，不改变 player 状态；runtime 定向独立通过。
  `SetHairScale/SetBustScale` 只记录幅度，`startWind` 不被当成已实现的物理模拟。

### 方案

分支决策，按实测游戏占比选择：

- **A（保守）**：保持显式不支持，补齐 `setOuterForce`/`setOuterRot` 的
  参数校验与首见日志，文档化。
- **B（近似）**：实现 bust/hair 一维弹簧-阻尼跟随（gravity/spring/
  friction/bendR），只驱动对应 part 链的旋转；`SetOuterForce` 作为外力注入。
- **C（完整）**：按驱动 `EPWindControl`/`EPBustControl` 语义建模，代价最高。

krkrsdl3 对照：其物理同样未实现（`updatePhysics` 空、`setOuterForce`/
`startWind`/`initPhysics` 为 TODO 桩），说明"显式不支持"是与现有成熟实现一致的
务实选择；**默认选 A**，仅当实测游戏确需物理时再启动 B。

### 验收标准

- 选定分支有 ADR/注释依据；B/C 需要真实模型逐帧对比截图。

---

## E6 — 加密 PSB 整包解密（`EmoteFilterTexture`）（P1）

**当前校正（原研究目标保留）**：LZ4 checksum/长度/边界加固及静态 review 已通过；
真实 v2 body 样本缺，最新全构建待做。driver-contract §6 已证明 `EmoteFilterTexture`
的 callback 面向 RGBA8 纹理与 mip pixel 缓冲，不是回调整个已解密 PSB；结构过滤全范围
未恢复。下方旧“整包过滤/固定 key”描述仅为历史假设，不再作为已证 API 或实现承诺。

### 问题与证据

- 导出 `EmoteFilterTexture(uint8_t*, size, callback)`（`0x10003920`）用固定
  key `851083516` + 与 `PsbCipher` 相同的 xorshift 对整包做过滤；
  `EmoteCheckValidObject(const uint8_t*, size)`（`0x10003ed0`）做
  "解密+可解析"校验；内部类 `PSBFilter`/`StructCryptFilter`。
- 历史只解头部；当前 `DecodePsb` 已有 v2 body 区间、宿主 seed、MDF/LZ4 支持，
  README 已改有限能力。v2 无校验，错误 seed 与坏 body 不可可靠区分；不声称认证。
- krkrsdl3 对照（`emotefile::load`）：密钥 = `{0x075BCD15, 0x159A55E5,
  0x1F123BB5, seed}`；seed 由宿主注入（`setEmotePSBDecryptSeed`），另提供
  `setEmotePSBDecryptFunc(buffer,len)` 自定义解密回调（游戏可在 XP3 过滤之外
  再叠一层）；**PSB v2 额外解密 `[offsetEncrypt, offsetChunkOffsets)`**；
  另支持 `lzfs`（`04 22 4D 18`）LZ4 帧容器。

### 方案

1. 用样例确定流的起点/长度语义（头部解密后 cipher 是否继续覆盖 body），
   在 `psb.cpp` 增加整包 filter 路径（复用 `PsbCipher`）。
2. `DecodePsb` 失败信息区分"头部校验失败"与"body 解密失败"。
3. 合成回归：用已知明文按该算法加密 body，验证能解回；`psb_encrypted_regressions` 扩展。

### 验收标准

- 真实整包加密模型可加载；合成加密回归通过；README 能力表更新。

---

## E7 — 多文件（拆分）PSB 归档（P1）

### 问题与证据

- `createEmoteLayer{files={a,b,...}}` 当前显式拒绝（`lua_engine.cpp`
  `multi-file E-mote archives are not supported`）。
- 驱动有 `sourceIconRenameMap` 字符串，但不足以确认拆分 ABI、重编号、冲突优先级
  或合并规则；旧“支持拆分导出”的推断未获样本证实。当前仍缺拆分样本。

### 方案

1. 从样例确定拆分格式（各部分 PSB 的资源如何拼接/重命名）。
2. 在 `l_createEmoteLayer` 中合并多文件为一个 `PsbDocument`（资源偏移重写 +
   `sourceIconRenameMap` 应用），失败时事务性拒绝。
3. krkrsdl3 走的是"附加文件"路线（`_attach` + `addEmoteFile`：主文件 + 附加
   文件共同参与 chara/motion/变量查找，不做物理合并）——可作为备选实现路线，
   先看样例文件的实际拆分语义再二选一。

### 验收标准

- 拆分模型与合并模型渲染一致；错误路径不破坏已有图层。

---

## E8 — 贴图格式补齐（P2）

### 问题与证据

- 驱动支持字符串：`A8L8`、`RGBA5650`、`RGBA5551`、`RGBA4444`、`RGBX8`、
  `DXT1`、`DXT3`、`ETC1`、`PVRTC_4BPP`、`PVRTC_2BPP`、`BC7`、`DXT5`，
  以及 `mipMap`/`mipMapLevel`。
- 项目仅 RGBA8/CI8/DXT5/BC7（`emote_model.cpp:78-115`）。

### 方案（已落地）

按实测游戏命中顺序补齐：

- **DXT1/DXT3**：`block_decode.cpp` 解块（BC1 含 1-bit alpha 打孔模式，
  BC2 显式 4bit alpha），合成回归覆盖不透明/打孔/alpha 三种情况。
- **16bit**：`RGBA4444`/`RGBA5551`/`RGBA5650`，以及 `A8L8`（亮度+alpha）、
  `RGBX8`（丢弃 X 字节，桌面序仍按 A8R8G8B8 换序）。
- **mipMap/mipMapLevel**：带 mip 描述符时接受 level 0 之后的附加数据并只读
  level 0；无描述符时仍要求精确长度。
- **ETC1/PVRTC**：实测（甜蜜女友3 DXT5、krkrsdl3 桌面路径）均无命中样本，
  维持 `unsupported E-mote pixel format` 显式拒绝；待移动端图集样本再补。

krkrsdl3 对照：其只支持 `RL`/`none` 与 RGBA8/调色板展开，格式上不提供借鉴；
但暴露了一个本项目缺的**图集裁切**语义：`spec=win` 的 icon 带
`clip{left,top,right,bottom}` + `texWidth/texHeight`，`pixel` chunk 是整张
图集，需按 `left/top` 裁到 icon 尺寸（`readIconTobuffer` 的裁切分支）。
本项目现在把 chunk 当整图解析，遇到图集模型会报长度不符或错位，需要一并补。

### 验收标准

- 对应格式的裁剪图标像素与官方对照一致（色彩序/alpha 语义）；
  `tests/emote_regressions.cpp` 的合成块已覆盖。

---

## E9 — 场景节点：模板/遮罩合成、网格扩展、粒子（P2）

### 最新执行补记（晚于下方 owner/mask 返修记录）

- 有效 coordZ 与 icon metadata 全图吻合是限定观察；**Z 通道已落地并移除 label hacks**，
  仅获有限静态 review，不代表官方完整同状态一致或 E9 关闭。meshSync red13→green，
  无 frame 时 suspend；meshCombinator 数学 core 与预算（总64 MiB/单8 MiB/4096条）
  及 standalone ASAN 已过，combinator 已接入 scene；同域 meshCombine（combinator 优先于 bp）已实现。
  低位 sync、cc、屏障 anchor 补偿、异域等降级加载并记录 UnsupportedFeatures（不应用），
  真实 132 个 fg 模型 DecodePsb/Model/Player Load 全部成功（仅加载级；每个模型命中
  clampControl.type0/1、meshSync.lowChannels、meshCombine.fallback，131 个有 timeline.openEnded），
  `EmoteScene::Load` 经图集解码缓存由约 4.2s 降到约 50ms。
  曲线、模板/粒子与最后 GL/官方精确状态继续分别验收。
- **stencil 蒙版缓存缺陷已修（本机读档双立绘：眼白无虹膜）**：`EmoteScene::Render` 原按
  标签列表哈希分配 stage 蒙版纹理并按标签列表缓存，导致 (1) 同标签的左右被蒙版部件
  （如两只眼）共用首个部件解析出的最近源；(2) 两个 owner 的同标签集共用一张纹理，后渲染者
  覆盖前者。现按“owner + 实际解析出的源集合”分配/缓存，源集合变化或 `Remove` 时释放
  （`Compositor::RenderStageMask(string,…)`/`ReleaseStageMasks`）。回归：
  `compositor_regressions`（CGL）合成双 owner、同标签左右源用例。属 HEAD 已存在的潜在缺陷，
  非本轮 Z/combine/mirror 回归；仍无官方同状态逐像素验收。
- `ARTC_EMOTE_HIDE` 已修成精确 part 隐藏、不再隐藏整个节点树；probe 已改为明确
  的 production 顺序候选报告。其采样仍不是最终 framebuffer 像素归属，mesh/mask/blend
  等限制保留。下方“本轮未修复”仅描述之前阶段，不是当前状态。
- 原打开失败模型、被修复项 GL 官方对照、未支持项明确处理等验收全部保留；
  最新 Z/combine 不可沿用旧 owner/mask 合成通过记录作为验收。

### 问题与证据

- 加载期显式拒绝（`emote_scene.cpp`）：`stencilType!=0`、
  `coordinate`/`groundCorrection`、`inheritMask` 非默认、
  `transformOrder` 非默认排列、motion variable initializer、
  非 0/2/3 节点类型、child motion `mask`/未知控制字段。
- 驱动元数据出现：`stencilCompositeMaskLayerList`、`meshTransform`、
  `meshSyncChildMask`、`meshDivision`、`meshCombinator`、`meshCombine`、
  `objTriPriority`、`particleMotionList`、`particleMaxNum`…。

### 方案

1. 先收集实测游戏中触发的拒绝日志（错误信息已带节点 label），按出现频次
   排序。
2. 优先实现最常见的 1–2 项（大概率是 `stencilType`/遮罩合成或网格扩展），
   其余维持显式失败。
3. krkrsdl3 已有可参照的实现面：`meshDivision` 运行时 CPU 细分缓存、
   `meshCombine`/`meshTransform`/`meshSyncChildMask`、节点 `priority`
   重排（`motion.priority[0].content` 的索引列表决定求值顺序）、帧曲线
   `zcc/ccc/cc`、per-frame `color`（缺省 `0xff808080`，`hasColor` 区分）、
   `blank:w:h:ox:oy`/`shape/...`/`clip` 源、`stencilCompositeMaskLayerList`
   + maskTarget 渲染。实现前先对照本项目拒绝清单，按需选做。

### 已落地（甜蜜女友3 立绘证据）

- **per-frame `color`**：8 处全为中性 `0x808080ff`，确认是 MODULATE2X 空间；
  按 `2*c` 折算到直线因子并与播放器 tint 相乘，逐 part 下发
  `colormultiply`（数值/数组两种写法都接受）。
- **`blank` 描述符**：真实格式是 `icon="w:h:ox:oy"`（无 `blank:` 前缀），
  解析后作为布局原点；`blank:w:h:ox:oy` 写法兼容保留。
- **`priority`**：每个 motion 都有。按参考语义（content 反向枚举，
  末项 rank 0 先画；缺项顺延）对直接子节点做稳定重排。该作 content 均为
  恒等反序，视觉不变；其他游戏若真重排也按参考行为落地。

### 实机修复（TyranorNext + 甜蜜女友3，2026-09-30）

- `inheritMask` 白名单过严：真实立绘使用 `0x7FC/0x20007FC/0x200020C/
  0x24007FC`，放宽为接受任意数值；部分继承（低 0x1FC 位不全）暂按全继承并
  一次性日志。
- 内容 `bm` 不再拒绝：低半字节映射 add(1)/reverse-subtract(2,5)/multiply(3)/
  screen(4)，`layer_shader` 新增 multiply 与 reverse-subtract 混合函数。
- `createEmoteLayer` 的 width/height 现在生效：模型原点位于盒中心
  （参考宿主 `model_origin`），修复立绘整体偏移。
- **`Animated::Set(v, 0, …)` 与 `Animated(v)` 现在同步写 `target`**：此前
  `Skip()/Pass()` 的 `Finish()` 会把 setScale/setCoord/setColor 的即时值
  全部归零，立绘整体不可见（实机主症状）。
- HOLD（type 0）帧：节点自身不绘制，但子节点照常递归（对齐参考宿主
  `visit_layer` 无条件访问 children）。

### stencil/mask 合成（已落地）

- 求值期按参考语义传播 `stencilType & 0x4 + stencilCompositeMaskLayerList`：
  后代继承，声明节点替换整张列表；容器自身不绘制、子节点照常渲染。
- 绘制期把 mask 标签解析到最近的同名可绘制 part（参考宿主按 source_label +
  中心距离匹配），将该 part 及其子树渲染进 **stage 空间蒙版纹理**
  （`Compositor::RenderStageMask(key, ids)`，每个标签集一张，`SetLayerStageMask`
  挂到被裁剪 part；`LayerEffect.stage_mask` 走 coverage 通道，按蒙版
  **alpha** 乘算）。
- mask 源未绘制时按参考（krkrsdl3）"蒙版层未绘制 → hasStencil=false"处理：
  不挂蒙版、保持未裁剪，而不是用空蒙版把部件裁没。
- 回归：合成标签传播（emote_regressions）、GL 蒙版裁剪像素断言
  （compositor_regressions `ARTC_TEST_CGL`）。

### meshSyncChild / 粒子（早期结论与修订）

- **已撤回的历史结论**：早期对 132 个立绘的统计记录为
  `meshTransform=1`、`meshSyncChildMask` 形状位 10804 处、`meshCombine=1`
  2966 处，却以 `bp_nonempty=0, cc_nonempty=0` 推断“全部无载荷、meshSync
  是 no-op”，并据此结项。这一载荷判定已被后续发现的帧级 4×4 Bezier
  控制网格推翻，**不能再作为当前能力或结项依据**。
- **当前状态**：已实现有限的形状形变作用域，见下节；这不等于完整支持
  meshSync 各通道、`cc` 或 meshCombine，仍待更充分的合成回归。
- 粒子：`particleMotionList`/`particleMaxNum` 在真实模型中零命中，维持显式
  跳过（文档化）。

### 本机（macOS 宿主）验证与修复（2026-09-30 晚间）

- 给 `artemis-mac` 增加开发用注入：`--tap x,y@frame`（按 60fps 帧号注入
  stage 坐标点击，模拟真实 touch：hover + press + release + click）与
  `--snapshot out.png@frame`（全分辨率合成快照）。本地即可复现存/读档与
  渲染问题，不再依赖真机。
- **历史局部诊断，非官方脸部一致验收**：当时用存档缩略图参考定位并修复下列两项；
  未同步完整变量/姿势，不能据此声称它们是全部根因或脸部已与官方一致：
  1. **stage 蒙版纹理被所有标签集共用**：每次 `RenderStageMask` 覆盖同一张
     纹理，所有被裁剪部件实际采样到"最后一次渲染的蒙版" → 脸部被错误裁剪/
     出现色块。改为按标签集分配独立蒙版纹理（`RenderStageMask(key, ids)`）。
  2. **蒙版解析是 O(部件×图层) 的 `EffectiveRect`**（每次调用都走祖先链）：
     双立绘场景掉到约 3fps（300 帧耗时 100 秒），表现为"点击卡死"。改为
      每帧先缓存所有部件中心（O(n)），再按标签分组匹配；历史记录称性能恢复，
      不是最新源码可复现的帧率量化验收。
- 本机复现结论（全分辨率快照）：标题→读取存档→槽位→YES 确认→读档成功
  （`load: checkpoint restored`）；游戏内 Save→槽位→覆盖确认→写档成功
  （`save: checkpoint save0004.dat committed`），存档缩略图正常。
- **meshSyncChild 有限形状形变已落地，待更充分回归**（此前的"无载荷"结论是错的：`eye_pos`/
  `mabuta`/`hi_pos`/`eyebrow_pos` 等帧携带 4x4 Bezier 控制网格）：
  求值期按 `meshTransform` + 形状位建立形变作用域（含 blank/图标
  extent、`inheritMask` 形状位挂起），渲染期对子部件生成 8x8 形变网格。
- **变换模型按已有行为分析重构（历史修复记录）**：当前实现在一个 motion player 内按
  `inheritMask` 组合线性状态（翻转移位/旋转/缩放/剪切各自独立），部分掩码
  （如 0x200081B 不含缩放位）要相对 **motion 根** 重建，嵌套 motion 以进入
  节点的仿射为新根；子层继承父层仿射，除非父层带 0x400000（透明父级）。
  旧实现"父链全量乘法"把表情/头发的旋转缩放错误地压到脸部件/脖子/追加
  部件上，导致贴图错位成硬边色块。现在求值期计算每个节点的绝对仿射并由
  部件层携带（布局层恒等）；已有截图仅支持局部改善观察，未完成官方精确状态对照。
- **后层绘制序（临时经验规则的历史记录）**：早期局部视觉参考中后发在人物之后、脖子/下巴之后；当时新增合成器
  `paint` 提示（`SetLayerPaint`，小于 0 先画/在后）：`後髪`=-1、
  头发下的 `追加パーツ`=-2、`■首`=-3。旧全局 `paint` 可跨背景/效果组改变顺序，
  作用域有缺陷；后经 owner 局部排序返修，最新 Z 通道已移除 label hacks；上述标签经验
  只留作历史，不是官方通用层序规则或当前实现。
- **官方截图的局部视觉参考（非精确 A/B）**：`■腕L`、`■前髪` 与其下
  `追加パーツ` 的局部遮挡曾用于提出“节点本体在挂载子部件之后”的假设。
  当前没有同步完整变量/姿势，截图不足以证明官方通用层序规则。新增的
  `SetLayerEmoteTree` / `DrawOrderLess` 的旧实现以 pairwise 祖先判断将 `.000000`
  本体放在子部件之后，已被审查证明在混合普通层时不满足严格弱序。该比较方式
  本轮已替换，返修后合成运行及独立复审通过；局部补片改善仍不等于官方层序已确认。
- 诊断（env 门控）的边界：`ARTC_EMOTE_PIXEL=x,y` 只是按逆仿射映射采样
  **原图候选 texel**（含 RGB/alpha/et/paint），忽略真实绘制序、mesh、mask、
  blend 等，**不是最终像素归属**。`ARTC_EMOTE_HIDE=icon` 的旧实现匹配图标后
  隐藏整个节点树，不是只隐藏该图标；不能把其视觉变化当作单图标因果证据。
- 本机验证：`say_6`/`shi_36` 单模型预览（CGL）和游戏内快照（1920×1080）
  曾记录脸部/发型改善、无整块色斑；`fin3950.png` 与手机官方截图仅作局部
  视觉参考，不能称为完整姿势/变量同步后的逐区一致验收。
- 待核实线索：`shi_36` 手部附近 `■追加パーツ`(0138) 的白底露出差异
  目前仅有文字描述，缺少精确状态对照及持久证据；不能断言官方被手/裙遮住，
  也不能据此确定层序或蒙版根因。

### 本轮审查状态与验收边界（2026-10-16）

> **历史时点说明**：下列“本轮”指更早 owner/mask/effect 返修阶段；新 coordZ/combine、
> HIDE/probe 修复、23/23 sync 基础与 E14 工具/真实 host 观察已在顶部和各 E 补记。
> 保留旧结果及当时限制，不将“无游戏运行”“probe/HIDE 未修复”等旧快照当作最新状态。

- `tests/compositor_regressions.cpp` 已新增双 mask-key 独立性、`ReleaseGl`
  释放/重建、非对称 coverage 及显式/嵌套 intermediate 覆盖。非对称用例抓到
  stage 蒙版 Y 翻转，当前修复为 `Compositor::RenderStageMask` 设置
  `u_top_down=1`。早期主 agent 独立 CGL repeat 3 次通过；后续 mask 方向
  独立只读审查通过，合并后运行结果另列如下。
- **层序返修完成，独立 review 确认 P1 可关闭**（限定合成运行/静态审查边界）。已改为 owner 局部的
  `effect-path → paint → postorder rank → id` 全序，并将 `priority` 接到实际
  Draw 顺序，新增合成回归。不宣称真实游戏或官方等价。契约为：
  - owner 必须有效驻留且互不重叠；**嵌套独立 owner 明确拒绝（返回 `false`），不是已支持**。
  - 整体 owner 或其祖先 rename 会映射 owner 并保留 rank/hint；同 owner 内 rename 保留状态。
  - 显式成员及 ordinary 附加成员跨 owner 或移到 owner 外时，清除源 paint/rank/标记。
  - 冲突 rename 正常返回 `false` 并原子拒绝；delete 后旧 ID 重用无 ghost。
  - `HitLayer` / `HitLayers` 复用真实 Draw 候选顺序，但仍是几何命中，不是 mask 像素命中。
- **效果状态最小修复**：显式回写 `grayscale=0`、normal 混合与白色 tint，
  此前 9 项失败已消除；新增固定 ID/图层保留回归，effect-reset 独立 review 已通过。
  probe/HIDE 的上述限制本轮未修复。
- **主 agent 独立执行的返修后验证记录**：
  - `cmake --build build-test -j8` + ctest：**22/22** 通过。
  - `cmake --build build-cgl -j8` 全构建 + ctest：**24/24** 通过，含视频与 compositor。
    更早合并版本的 CGL `compositor_regressions` repeat **5 次**通过，保留为阶段记录。
  - `build-mac` 的 `artemis-mac` 构建成功。
  - `build-android` 构建成功，`tools/check_android_exports.py` 检查 **12 入口**通过。
  - **末次 owner 尾项之后**：主 agent 已独立重建 `build-consumer`，ctest **1/1** 通过；
    重建 `build-consumer-android`，无 JNI consumer 的 final shared link 成功；
    `git diff --check` 通过。
    Android 交叉产物不能在 Mac 运行，链接成功不等于运行通过。
  - 已有 warning 非新增错误。更早的 22/22 仅是 **Y 翻转修复前 baseline**；
    不与本次返修后重新构建/运行混用。以上结果不替代真实游戏与官方状态对照。
- **交付边界与下一步**：交付时索引状态以 `git status` 为准，暂存由主 agent 处理，
  本轮不提交、不发布、不复制 plugin、不改 manifest，未对 v29 做本轮验证；
  无游戏/手机运行，未触设备存档。仍需关注排序键构建中的祖先扫描性能；后续需受控
  本机游戏视觉/快照、手机官方完整状态对照及 0138 证据闭环，probe/HIDE 限制仍在。
  Android 宿主实际合同及 JNI 已知缺口仍待验证（见 `docs/embedding.md`），导出检查及
  无 JNI consumer 链接成功不等于 JNI 行为兼容。历史设备记录不替代本轮验收。

### 备注

- 尾部 HOLD 语义：动作用尽后**保持上一有效帧继续绘制**（官方立绘长时间常驻），
  并用 1ms 帧时间容差消除嵌套时钟浮点误差（此前会冻结在 60.999 帧、恰好
  掩盖该语义）。
- 真机"点击没反应"实为上面的 3fps 回归；"画面被缩小/切后台重启"属于宿主的
  窗口尺寸与进程策略（引擎 stage 固定 1920x1080，本机快照已确认），待设备
  接回后在 TyranorNext 侧核对。
- 历史默认预览与游戏流程对后发的观感不同，曾归因为工具默认状态；由于缺完整
  同状态官方 A/B，这只是当时解释，不能据此排除引擎层序缺陷或宣称官方一致。

### 验收标准

- 打开失败的模型；被修复项在 GL 下与官方对照；未修复项仍在加载期明确报错。

---

## E10 — 控制类元数据：眨眼/口型/选择器/orbit（P1）

**当前状态**：transition / immediate / profile / orbit 已接入 model/player（`emote_control.*`，
`emote_control_regressions` / `emote_control_player_regressions`），mirror 三态已对齐，
多 blink 独立时钟/大步长已修；clamp / stereo / eye edge / eyebrow nodes / mouth shape
未实现。`build-test` 27/27 为本机合成回归，真实游戏快照中未观察到眨眼（不判定存在与否）。
逐键状态见[能力证据表](emote-capability-evidence.md#2-e10逐键判定表)。
官方 SO normal consumers 已确认 transition/immediate/clamp/eyebrow/mouth/orbit/profile
与精确 face 默认名的运行时归属，改标 `official-runtime-static-confirmed`，项目支持另列。
地址/样本口径及算法边界见能力表与 driver-contract；未实现的控制器不声称已支持。
P1-A：transition、immediate/default、mouth/talk；P1-B：clamp、eyebrow、orbit、特殊 mirror；
P2：profile 查询。各项实现前置证据见能力表排期，clamp type0 等未知算法不能猜填。
旧“三项＝E10 完成”撤回；排期非动态兼容，原真实流程验收保留。

### 问题与证据

- 元数据键：`transitionControl`、`selectorControl`、`immediateControl`、
  `clampControl`、`mirrorControl`、`eyeControl`、`eyebrowControl`、
  `mouthControl`、`bustControl`、`hairControl`、`partsControl`、
  `orbitControl`、`charaProfile`、`variableMatchList`、`optionList`/
  `offValue`/`onValue`、`blinkIntervalMin/Max`、`blinkFrameCount`、
  `blinkEnabled`、`talkLabel`、`face_cheek`/`face_tears`/`face_eye_hi`。
- 这些多数由模型编辑器生成；需判定哪些是**引擎/驱动运行时功能**
  （自动眨眼、口型同步、选项选择），哪些只是编辑器标注。

### 方案

逐项找证据（驱动代码引用 / 游戏脚本调用 / E-mote 官方手册），产出
"运行时 vs 编辑器"判定表；运行时项进入实现队列，编辑器项明确忽略。

krkrsdl3 已证实两项是运行时功能，建议优先实现：

- **自动眨眼（eyeControl）**：参数 `label/beginFrame/endFrame/
  blinkFrameCount/blinkIntervalMin/Max`；空闲时随机等待 `[min,max]` 帧，
  到时按 `blinkFrameCount` 做"闭→开"两端线性动画写入变量，播完后恢复
  `baseVal` 并重新等待。注意：带眨眼/口型的立绘不能让 `animating` 恒真，
  否则等待动画结束的对话流程会挂起（krkrsdl3 有 Nekopara2 类事故记录）。
- **选择器（selectorControl）**：每项 `{label,onValue,offValue}`；
  `setVariable(selectorLabel, opt)` 选中项写 `onValue`、其余写 `offValue`
  （加载时先 select 0）；时间轴控制跳过被 selector 接管的变量。
- **attrcomp**：`{chara,motion,layer,value}` 规则，`value<=0` 时在加载期把
  目标节点标记为不渲染（本项目未解析，会导致应隐藏的层仍然显示）。
- 历史参考实现不驱动 eyebrow/mouth 等不能再用于归属判断：官方 normal path
  已证 eyebrow edge/node 求值、mouth label 整数/talkLabel 浮点由 SetVariable 驱动，
  不直接读 audio；精确 face_cheek/tears/eye_hi 是缺 immediateControl 键时的默认变量名
  与 instant 集合（显式空表不 default），不是任意 face_* 通配。项目实现按前置证据排期。

### 验收标准

- 判定表齐全；被判定为运行时的项有实现或明确的排期。

---

## E11 — `SetColor` RGB tint（P2）

### 问题与证据

- 驱动 `SetColor %d %f %f` 会乘色（着色器有 `colorMultiply`）；
  项目 `EmotePlayer::SetColor` 只改 alpha（注释称"合成器没有 RGB tint"，
  但 `Compositor` 已有 `colormultiply` prop 与着色器 uniform）。
- 相关：模型自带的 `metadata.mirror` 位（krkrsdl3 在渲染矩阵上做
  `scale(-1,1,1)` 自动镜像）本项目未解析，只支持 Lua `setMirror`。

### 方案

1. 在 `EmoteScene::Render` 的 part 层 props 中下发 `colormultiply`
   （由 `EmotePlayer` 传入的 RGB）。
2. 更新 `emote_player.h` 注释与回归（round-trip + GL 像素）。

### 验收标准

- `setColor(0x80ABCDEF)` 的 RGB 分量在 GL 回归中可见；alpha 行为不变。

---

## E12 — `CalcLayerFrameInfo` 暴露决策（P2）

**当前决策**：保留不暴露 Lua；EmoteScene 只覆盖自有求值用途，不代表 native 输出
结构兼容。完整签名、逐层结构/所有权/有效期未恢复；未建立新增公开需求不等于所有
游戏均无调用。决策已记录，native ABI 未知不作完成处理，详见能力证据 §3。

### 问题与证据

- 驱动 `CalcLayerFrameInfo %f`（`0x1000b370` → 实现 `0x1000b490`）计算
  逐层帧信息，供引擎侧合成；`fcn.1001ae50` 记录/回放中也有该命令。
- 本项目自行完成场景求值与合成（`EmoteScene`），Lua 代理目前未暴露。

### 方案

在 E1 契约表中明确该方法的输出结构；确认游戏脚本/引擎层是否调用。
若不需要则文档化"由 EmoteScene 覆盖"的结论并关闭。

### 验收标准

- 有结论（实现/不实现）及依据。

---

## E13 — 驱动 record/replay 作为差分验证工具（P3，可选）

**当前决策**：Windows/D3D9 参照缺，本轮选择不启动可选动态工具，不称真实轨迹
diff 通过；不阻塞其他独立项。若重启，仍须满足下方真实会话 diff 与工具归档验收。
`+0x1dc` 是命令列表相关结构，旧称连续文本缓冲不准确，见 driver-contract §7。

### 问题与证据

- 驱动内建记录/回放：`player+0x1d8`（0=normal/1=record/2=replay），文本
  缓冲区 `+0x1dc`，解析器 `0x1001ae50`（命令即附录 A 的 18 个 Set* 调用）。
- 可借此把官方运行中的 API 调用录成文本，在本项目端逐条回放并比对。

### 方案

1. 在 Windows 参照环境用官方驱动录一段游戏会话（或用 `EmoteCheckValidObject`
   自测样本）。
2. 写一个离线回放夹具（tests 内），把记录文本转换为 `EmotePlayer` 调用，
   对逐帧图层变换做数值 diff。

### 验收标准

- 至少一段真实会话的轨迹 diff 无未解释差异；工具脚本纳入 `tools/`。

---

## E14 — 实测游戏与 GL 冒烟矩阵（持续）

### 最新工具与真实运行观察（非整矩阵通过）

- `tools/emote_validation.py` 与 host 的 save-dir/frames、timeout/completion 管线
  已完成，工具 16 selftests 据主 agent 记录通过。每次用新的独立 savedir；
  采集完成须退出码、超时状态与所请求产物共同满足，产物存在不等于视觉通过。
- 主 agent 独立运行真实 host **1200 帧**，有标题快照、独立 savedir 与 **exit0**；
  **该次没有立绘**，不能完成本表 E-mote 场景验收。
- **newgame 2600** 受取名 name modal 阻塞并超时，不能算流畅游戏/对话通过。
  需受控跨过 modal 后继续原矩阵，不能把标题页运行与游戏主流程合并报喜。
- 官方内核同状态逐像素对照（手机）进行中，0138 未闭环；不把整个 E1–E17 goal 标为 blocked。
  Mac 读档复现路径与隔离存档目录规则见顶部账本；此文档不操作设备、游戏或存档。
- 最后代码稳定后重跑下列原命令及 detail DoD，并记录二进制/源码、实际命令、
  日志/产物时点；保持 JNI/原壳/Tyranor-Next 验证要求，链接/导出不代替行为。

| 场景 | 检查点 |
|---|---|
| 标题 → 对话推进 | E-mote 图层存在、推进无卡死 |
| 灰度/回忆闪回 | `setGrayscale` 过渡平滑，退出后复原 |
| 表情/变量切换 | `setVariable`、`setVariableDiff` 渲染正确 |
| 时间轴淡入淡出 | `fadeInTimeline`/`fadeOutTimeline` ease 生效 |
| 混合过渡 | `setTimelineBlendRatio(label, ratio, time, ease, flags)` 过渡与自动移除 |
| `step()` 驱动 | 逐帧动画推进正常，无双重计时观感 |
| 复杂模型 | 记录加载期"unsupported E-mote …"日志，回流 E8/E9 |

必跑命令（AGENT.md §9）：

```bash
cmake -B build-test -DCMAKE_BUILD_TYPE=Release -DARTC_BUILD_TESTS=ON
cmake --build build-test -j8 && ctest --test-dir build-test --output-on-failure
cmake --build build-mac -j8 --target artemis-mac
cmake --build build-android -j8
# 有 ANGLE 时：
# cmake -B build-gles -DARTC_BUILD_TESTS=ON -DARTC_TEST_GLES=ON && ctest --test-dir build-gles
# macOS 无 ANGLE/无窗口（离屏 CGL，等价 EGL pbuffer）：
cmake -B build-cgl -DARTC_BUILD_TESTS=ON -DARTC_TEST_CGL=ON
cmake --build build-cgl -j8 --target compositor_regressions && ./build-cgl/tests/compositor_regressions
./build-host/artc drive <游戏目录>/root.pfs --frames 400
```

---

## E15 — 同步点与动画结束语义（P1）

**当前官方静态结论**（固定 Android SO 身份/hash 见能力证据与 driver-contract §1）：
`RegisterLua 0x5509b0` 在本议题相关名字中只注册 isAnimating，不注册 skipToSync、
contains、hitTest。**保持 Lua 不注册是已有正面依据的决策**，不再写作“等待需求不明即可加”。
`MMotionPlayer::SkipToSync 0x6ab794` 在非 loop 路径查询 motion.tag 的
`type=1,time>=current,content.sync`，未命中 fallback lastTime；不是递归非 parameterize
最大值，没有 selfSyncTime 字段。`IsSyncWaiting 0x6abb24/Release 0x6abb2c` 是独立
eventwait；IsAnimating 仍针对 controls/playing timelines，不能合并成新结束三来源优先级。

旧 `SyncFrame` 与 C++ `SkipToSync` 保留为 **project legacy extension**，原独立 target
仅验证其自有推导/跳帧/事务，不证明 native tag sync。只读 TagSync 查询
（`EmoteModel::NextMotionSyncFrame`）已实现并有合成回归（emote_sync_regressions），Lua 不注册；
真实等待/同步流程未验。下方参考方案与验收条文保留追溯，但
sync→selfSync→last 的移植前提已被本次官方正面事实推翻，**不能照搬为实现要求**；
原真实对话不挂起/不提前要求不取消，须按 tag sync/eventwait 正确分层补证。

### 问题与证据

- krkrsdl3：`syncTime` = 该 motion 下**非 `parameterize` 节点**的最大有内容帧时刻
  （递归子 motion），`selfSyncTime` 为文件层值；播放结束判定优先取
  `syncTime`，其次 `selfSyncTime`，最后 `lastTime`；`skipToSync()` 直接把
  `clockPassed` 跳到 `getSyncTime()`。
- 本项目目前只按 `timeline.last_time`/`loop_end` 判断时间轴结束，没有"同步点"
  概念；对话等待（`[wait emote]` 之类）在带眨眼/口型控制的立绘上可能挂起或
  提前结束。

### 方案

1. 解析 motion 的 sync 相关字段（`syncTime`/`selfSyncTime`；krkrsdl3 的
   `syncTime` 由帧数据推导，若 PSB 另有 `sync` 字段需一并确认），
   在 `EmoteModel` 中建立 motion → sync 表。
2. 在 `EmotePlayer` 增加 `SkipToSync()` 与结束判定顺序（sync → last），
   并在 Lua 代理注册 `skipToSync`。
3. 回归：合成 motion 含 sync 帧，验证结束时刻与 `skipToSync` 落点。

### 验收标准

- 带同步点的实测游戏对话流程不挂起、不提前；合成回归覆盖三种结束来源。

> 官方新事实旁注：条文原样保留；“三种结束来源”来自其他宿主参考，其前提已被固定
> Android normal-path 证据推翻，不将其实现/勾选为官方兼容要求，也不偷偷删去历史约束。
> 后续须显式解释与原条文的差异，补 native tag 命中/fallback、loop 边界及 eventwait
> 独立性证据；动态真实流程要求仍有效。TagSync 只读查询包未完成，不能先勾选。

---

## E16 — 命中测试：`contains` / `hitTest` / shape 源（P2）

**当前状态**：几何命中（quad / mesh / shape）、三角形容差修复、stage full-affine/shape leaf 已落地，
合成定向通过；官方点击/遮挡/坐标一致性缺。旧“目前不注册”是立项时快照，当前已有
Lua contains/hitTest、shape/blank/clip 与有限几何查询，不代表 mask 像素命中或整项完成。
下方点/边/顶点、缩放旋转与真实游戏一致性验收仍全部保留。

**接口归属更正**：固定 Android `RegisterLua 0x5509b0` 不注册 contains/hitTest；
native `Contains 0x675d5c` 是 shape type1/geometry 查询。项目两个 API 统一 stage
坐标是自有扩展，没有官方 Lua 双API/该 stage 坐标协议证据；下方 KiriKiri 两者区别
只是该参考宿主行为，不能写成我们已实现的官方接口区别。

### 问题与证据

- krkrsdl3：渲染时收集 shape 节点几何（`shape/rect|circle|point|quad`，
  单位方格 16×16）与非 shape 的可见网格，构建 `EmoteHitFrame`；
  `hitTest([label,] x, y)` 用目标像素坐标、`contains` 保留旧仿射坐标；
  越界几何按渲染目标裁切。
- 本项目目前不注册 `contains`/`hitTest`（走 logging stub），也不支持
  `blank:w:h:ox:oy`、`shape/...`、`clip` 源。

### 方案

1. `EmoteScene::Evaluate` 输出中标记 shape/blank 层并保留其变换与网格。
2. 在 `EmotePlayer` 提供点包含查询（网格三角形判定），Lua 侧
   `contains(label?,x,y)`/`hitTest(label?,x,y)` 两种重载。
3. 回归：合成 shape 节点（矩形/圆/四边形）做包含/排除断言。

### 验收标准

- 实测游戏中立绘点击/触摸判定与官方一致；回归覆盖点/边/顶点与缩放旋转。

---

## E17 — 播放状态序列化（P3，按需）

**当前决策**：按需求门槛延期，不声称所有游戏均不需要。自有 SaveSnapshot/ARCV/
LoadSnapshot/onLoad 重建链已只读审阅，现有 callback 合成断言不证明 emote 精确恢复；
表情/表达变量、timeline 与 blend（含剩余过渡）证据仍缺。可用自有合成 restorer 补证，
无需复制商业脚本；原“官方一致或明确不需求”要求保留，详见能力证据 §4。

### 问题与证据

- krkrsdl3 暴露 `serialize()/unserialize()`，把播放进度/变量/时间轴状态存入
  存档并在读档后恢复。
- 本项目存档（BOWS/BOWG 导入器）不含 E-mote 播放状态；读档后立绘会从初始
  状态重建。是否需要在存档中恢复 E-mote 状态取决于实测游戏的存档脚本。

### 方案

先用真机确认游戏是否有 emote 状态写档/读档需求；有则按 `EmotePlayer` 内部
状态设计紧凑序列化（变量/时间轴位置/blend），挂到现有存档管线。

### 验收标准

- 存档→读档后立绘状态（表情/时间轴/blend）与官方一致（或明确不需求）。

---

## 风险与未决

- **E2/E3 的推断语义**（差分符号、flags/Pass/Step 分层）是本计划最大的
  不确定性来源；在真机验证前不要对外宣称完全兼容。
- **E5 物理**投入产出比低且无官方公式文档；krkrsdl3（KiriKiri 侧成熟实现）
  同样未实现物理，佐证"显式不支持"是当前务实选择。
- **E6/E7 依赖样例文件**：没有真实加密/拆分模型时无法闭环，需在任务开始
  时先收集样本。
- **E15 已有官方反证**：不能照搬 KiriKiri 的三来源优先级；native tag sync、eventwait
  与 IsAnimating 分开。保留 legacy 扩展需明示边界，TagSync 及真实流程仍待验证。
- 分析产物是 `/tmp` 临时文件，长期结论必须落盘到 `docs/emote-driver-contract.md`（E1）。

---

## 附录 A — 已恢复的官方方法契约（18 项，record/replay 格式串）

> **历史表，非完整 ABI**：保留原记录以便追溯；当前应读 driver-contract §3–§5。
> 下表 `time_ms`/`argb` 与 Pass“仅 20ms”不再当作格式串已证事实；记录参数形状不能
> 独立证明 C++ 类型、native 单位或颜色字节序。E2 ordered pair、E3 全清/Pass 完整路径
> 以正文最新更正为准，不把历史表的简写当成实现指令。

| 方法 | 参数 | 语义要点 | 驱动实现 |
|---|---|---|---|
| `SetCoord` | `x, y, time_ms, ease` | 容器位移 | `0x100140d0` |
| `SetScale` | `scale, time_ms, ease` | 单值等比（无 per-axis） | `0x10014280` |
| `SetRot` | `angle, time_ms, ease` | 角度 | `0x10014410` |
| `SetColor` | `argb(int), time_ms, ease` | 乘色 + alpha（初始化 `0x808080FF`） | `0x100145e0` |
| `SetGrayscale` | `value(0..1), time_ms, ease` | 亮度混合 | `0x100147e0` |
| `SetVariable` | `label, value, time_ms, ease` | 变量 | `0x1000bf30` / `0x1000c0e0` |
| `SetVariableDiff` | `label, pair, value, time_ms, ease` | pair 控制（单一值） | `0x1000d630` |
| `SetOuterForce` | `part(bust/hair/parts), 4×float` | 外力/物理 | `0x10014ca0` |
| `SetOuterRot` | `3×float` | 外旋 | `0x10015290` |
| `PlayTimeline` | `label, flags` | bit0 保留播放、bit1 逐层重置 | `0x100171b0` |
| `StopTimeline` | `label` | | `0x10017500` |
| `SetTimelineBlendRatio` | `label, ratio, time_ms, ease, flags` | flags=1 淡出到 0 后移除 | `0x10017710` |
| `FadeInTimeline` | `label, time_ms, ease` | 未播放则 `PlayTimeline(label,3)`，0→1 过渡 | `0x10017a50` |
| `FadeOutTimeline` | `label, time_ms, ease` | 等价 blendRatio(0,time,ease,1) | `0x10017bc0` |
| `Skip` | — | 立刻完成控制过渡并整体重算 | `0x1000b730` |
| `Step` | — | 推进一次控制过渡并整体重算 | `0x1000b920` |
| `Pass` | — | 对标记轨道做 20 ms 淡出 | `0x1000bb10` |
| `CalcLayerFrameInfo` | `time` | 逐层帧信息（引擎合成用） | `0x1000b370` |

ease 权重公式（两处实现一致）：`ease>=0 ? ease+1 : 1/(1-ease)`。

## 附录 B — 导出入口

> 保留历史导出表；FilterTexture callback 为纹理/mip pixel 缓冲，结构过滤全范围未知，
> 旧“整包 PSB 过滤”不等于已恢复完整输入/输出协议。当前以 driver-contract §6 为准。

| 符号 | 地址 | 说明 |
|---|---|---|
| `?EmoteCreate@@YAPAVIEmoteDevice@@ABUInitParam@1@@Z` | `0x100038d0` | `InitParam{host, alloc, free}` → `IEmoteDevice`（`PEmoteDevice`，vtable `0x1008bc08`，25 槽） |
| `?EmoteCheckValidObject@@YA_NPBEK@Z` | `0x10003ed0` | 解密/校验 PSB 缓冲 |
| `?EmoteFilterTexture@@YAXPAEKP6AX0K@Z@Z` | `0x10003920` | 整包 PSB 过滤（key `851083516`） |

## 附录 C — 复现分析

```bash
rabin2 -I -s -E emotedriver.dll            # 概览 / 符号 / 导出
rabin2 -z emotedriver.dll | grep -E 'Set|Fade|Play|Skip|Step|Pass|Calc'   # API 格式串
r2 -q -e bin.relocs.apply=true -c 'aaa; axt 0x1008cf20; pdf @ 0x100140d0' emotedriver.dll
```

关键虚表：`IEmotePlayer 0x1008b900`、`PEmotePlayer 0x1008ba88` 均为 95 项
（含析构，非析构各 94）；`IEmoteDevice 0x1008b898`、`PEmoteDevice 0x1008bc08`
各 25 项（含析构）。原 94/95 的差别是计数口径混用，不是接口增槽。

---

## 附录 D — krkrsdl3 `plugins/emoteplayer` 对照

> 路径：`/Users/weiss/github- engine/krkrsdl3/plugins/emoteplayer`
> （KiriKiri Z，BSD 三条款式许可）。仅提取行为/接口事实用于兼容规划，
> 不复制代码。规模：`emotefile.cpp` 2831 行（PSB 解析）、`emoterunner.cpp`
> 1251 行（求值/控制）、`emoteplayerclass.cpp` 1239 行（TJS 绑定）、
> `D3DEmotePlayer.cpp` 471 行（D3D 绘制）。

### D.1 该实现已做、本项目缺失（可借鉴）

| 能力 | 关键事实 | 计划项 |
|---|---|---|
| 自动眨眼 | `eyeControl` 参数 `label/beginFrame/endFrame/blinkFrameCount/blinkIntervalMin/Max`；随机等待→两端线性闭开→恢复 `baseVal` | E10 |
| 选择器 | `selectorControl` 每项 `{label,onValue,offValue}`；`selectValue(opt)` 只给选中项写 `onValue`；时间轴跳过 selector 变量；加载时先 select 0 | E10 |
| attrcomp | `{chara,motion,layer,value}` 规则，`value<=0` 在加载期把节点标记为不渲染 | E10 |
| 同步点 | `syncTime` = 非 parameterize 节点的最大内容帧时刻（递归子 motion）；结束判定 sync → selfSync → lastTime；`skipToSync()` | E15 |
| 命中测试 | shape/blank 源、单位方格 16×16、矩形/圆/点/四边形子类型、网格三角形包含、越界按目标裁切 | E16 |
| PSB 解密钩子 | 密钥 `{0x075BCD15,0x159A55E5,0x1F123BB5,seed}`；宿主 seed + 自定义回调；v2 解密 `[offsetEncrypt,offsetChunkOffsets)` | E6 |
| LZ4 容器 | `lzfs` magic `04 22 4D 18`，块校验/内容长度/字典标志解析 | E6 |
| 多文件 | `_attach` + `addEmoteFile`：主文件与附加文件共同查 chara/motion/变量（不物理合并） | E7 |
| 图集裁切 | `spec=win` icon 的 `clip{left,top,right,bottom}` + `texWidth/texHeight`，pixel chunk 为整图集按 left/top 裁切 | E8 |
| 网格/节点扩展 | `meshDivision`（运行时 CPU 细分缓存）、`meshCombine`/`meshTransform`/`meshSyncChildMask`、`priority` 列表重排 nodeList、帧曲线 `zcc/ccc/cc`、per-frame `color`（缺省 `0xff808080`，`hasColor` 区分）、stencil mask 列表 + maskTarget | E9 |
| 运动级变量 | `"motionName/varName"` 寻址与 `parameterCache`；变量→tick 映射 `division*(v-lo)/(hi-lo)`（与本项目一致，互相印证） | E10 |
| 状态持久化 | `serialize()/unserialize()`（变量/进度/时间轴） | E17 |
| 额外变换/查询 | `setSlant/setZoom/setFlip/setCameraOffset`、`getVariableFrameList`、`getCommandList`、`getLayerGetter/getLayerMotion`、`getPlayingTimelineInfoList` | 按需 |

### D.2 本项目更强 / 该实现未做（不必借鉴）

- 贴图：仅 `RL`/`none` + RGBA8/调色板展开；本项目另有 DXT5/BC7/CI8。
- 时间轴插值：仅线性，忽略 easing；本项目与官方 DLL 一致带 ease 权重。
- 时间轴淡入淡出 / `setTimelineBlendRatio` / `pass` / `skip` / `playTimeline`
  flags：均为 TODO 或直接忽略 flags。
- 物理：`updatePhysics` 空实现，`setOuterForce`/`startWind`/`stopWind`/
  `initPhysics` 全为 TODO 桩。
- 时钟：`speedRatio=20`（约 20ms/tick）与本项目 60fps 帧钟不一致，属 KiriKiri
  插件选择；以 Artemis 真机为准，不照搬。
- 基础运动求值：该实现在模型带变量时把基础 motion 固定在 tick 0（只用控制
  驱动）；本项目始终推进基础 motion。差异需用 Artemis 实测确认，勿直接改。

### D.3 对计划的修订

- E5 默认选项确认为 A（显式不支持）。
- E10 提升为 P1，并把自动眨眼 / 选择器 / attrcomp 列为最先实现项。
- 新增 E15（同步点）、E16（命中测试）、E17（按需持久化）。
- E6/E8/E9 的方案补充了上述对照细节。
