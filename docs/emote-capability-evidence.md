# E-mote 能力证据与决策边界（E10 / E12 / E17）

> 2026-10-16 当前时点记录；本文用文件与函数名定位，不固定易变行号。文档编辑本身不运行游戏、
> 设备或官方 DLL，不访问外部仓库，不复制商业脚本。本文是能力/缺口记录，不是 SDK 等价声明或发版记录。
> 静态事实、合成回归、真实游戏观察、官方对照分层标注，互不代证。

## 当前时点同步

全 E1–E17 的执行清单与 `implemented/synthetic/real/staticdecision/missing` 账本见
[主计划顶部](emote-plan.md#execution-ledger)，各 E 原验收均保留；计划未全部完成。

- **验证时点（本机合成与构建）**：`build-test` ctest **27/27**，`build-cgl` **29/29**
  （`compositor_regressions` 连续 3 次通过）；`build-mac` / `build-android` /
  `build-consumer` / `build-consumer-android` 构建通过；Android **12 个入口**导出检查通过。
  不代表真实游戏、设备或官方一致；导出/链接不等于 JNI 行为兼容。更早的 22/22、23/23、24/24、25/25
  是此前时点，不与当前混用。
- **E1**：120 行逐槽表 + 240 DWORD 独立校验通过；完整 ABI 未知。mirror 三个函数
  （`InitMirrorControl` / `MatchMirrorVariable` / `ReflectVariableValue`）的新旧地址已核对为
  “入口 vs 内部证据范围”粒度不同，非冲突。
- **E2 / E3**：ordered pair 控制器已实现（second 聚合、getter 分离、有限性）；mirror 三态
  （有效 = metadata XOR 用户状态，`Mirror` getter 返回用户状态）与 `variableMatchList`
  字节子串取负已实现，`setMirror` 是项目扩展（官方样本 Lua 无注册）。flags bit0=0 清全部、
  bit1 私有差分（非 sequential）、Step/Skip/Pass 按官方静态语义、独立 `setQueuing`/`getQueuing`
  已实现。**已知差异**：`PlayingTimelineFlagsAt` 仍返回 stored flags（官方为 `|1`，未改）。
  官方模型/轨迹对照缺。
- **E5**：`setOuterForce`/`setOuterRot` 专用校验 + unsupported；不是物理模拟。
- **E6**：LZ4 校验加固（有合成坏流用例）；真实 v2 body 样本缺。
- **E7 / E17**：多文件明确拒绝；自有 ARCV/onLoad 重建安全网不含 player 状态；均无样本/需求证据。
- **E9**：有效 Z 通道（去 label hacks）、完整 affine + shape leaf、warped mask source、
  meshSync 有限作用域、meshCombinator core + 预算（总 64 MiB / 单 8 MiB / 4096 条）+ scene 接入、
  同域 meshCombine（combinator 优先于 bp）已实现。低位 sync、cc、屏障 anchor 补偿、异域等
  为降级加载并记录 `UnsupportedFeatures`（不应用）。真实 132 个 fg 模型 DecodePsb / Model /
  Player Load 全部成功（仅加载级），每个模型命中 `clampControl.type0/1`、`meshSync.lowChannels`、
  `meshCombine.fallback`，131 个有 `timeline.openEnded`。`EmoteScene::Load` 经图集解码缓存由约 4.2s
  降到约 50ms。
- **蒙版缓存缺陷已修**：左右眼共用蒙版源、stage 蒙版纹理 key 跨 owner 碰撞，已按
  “owner + 解析源集合”修复；Mac 读档 say_6/shi_36 眼睛虹膜/高光/红晕恢复（对比图在本机
  临时目录，不进仓库）。无官方同状态逐像素验收。
- **E10**：transition / immediate / profile / orbit 已接入 model/player（`emote_control.*`），
  mirror 三态已对齐，多 blink 时钟已修；clamp / stereo / eye edge / eyebrow nodes / mouth shape
  未实现。真实游戏快照中未观察到眨眼（不判定存在与否）。
- **E14**：Mac 读档复现路径（标题约第 1500 帧才可交互；点击 `1684,293@1560`、`390,260@1700`、
  `806,497@1780`）与隔离存档目录规则见主计划账本。
- **E15**：TagSync 只读查询（`EmoteModel::NextMotionSyncFrame`），Lua 不注册（官方样本无 skipToSync）；
  旧 `SyncFrame`/`SkipToSync` 为项目 legacy 扩展。**E16**：几何命中（quad / mesh / shape）+ 三角形容差修复；
  官方 Lua 只有 `isAnimating`，无 contains/hitTest 注册，项目 `contains`/`hitTest` 为扩展。
- **仍缺**：官方内核同状态逐像素对照（手机，进行中）、0138 闭环、Android 宿主合同
  （官方壳与 Tyranor-Next 各自 Activity/插件）验收、插件重新构建发布（TyranorNext 内
  `libartemis-clean.so` 仍是旧 v29，需更新 manifest `pluginVersion`）；E13 不启动（无 Windows 运行环境）；
  E8 移动端压缩格式无样本。

格式与行为细节见[格式证据](emote-format-evidence.md)（Z、rawMeshList、meshCombine、sync 位）。
orbit 官方静态事实：首次 Progress 不消费 dt；整周期回到同一 index 不重发；tween 的
duration = `(index+1)*(T/N) - phase`（T 为周期，N 为段数，phase 为当前周期内相位）。

## 1. 证据口径

- 当前实现证据来自 `src/render/emote_model.{h,cpp}`、
  `src/render/emote_player.{h,cpp}`、`src/render/emote_scene.cpp` 与
  `src/script/lua_engine.cpp`；现有合成断言来自 `tests/`，本次仅审阅，未执行。
- 主计划 [E10/E12/E17](emote-plan.md) 与
  [详细计划 WP1–WP4 / WP11](emote-detail-plan.md) 提供历史需求与参照来源；
  历史外部实现笔记不视为本次重新核验的官方证据。
- 官方静态事实以 [驱动契约](emote-driver-contract.md) 为准；记录字符串、
  单个调用点、原生结构、Lua 接口与动态视觉兼容必须分开，不互相代证。
- **已实现运行时**：本项目有字段消费及可说明的求值路径；不代表官方全部语义已证。
- **official-runtime-static-confirmed / project-support-separate**：固定官方 SO 样本的
  normal-path 初始化/求值/查询消费者已确认；只判定官方运行时归属，项目是否解析、
  是否实现完整算法及动态兼容另记。下列 SO 地址不属于前述 PE DLL 地址空间；样本
  SHA/架构/地址口径及持久复现证据见 driver-contract §1/§9，本任务按静态研究交接
  引用，不加载 SO，也不编辑该契约。固定 SO 为 `arm64-v8a/libartemis.so`、9292144 bytes，
  SHA-256 `cb2b58d8080b29529b27a8453f602646ab5f13ae5d4acc59b673c27c27e2d9cc`；
  外层 APK/plugin ZIP 身份链见该文档，不能把这些地址混入 Windows PE 槽表。
- **明确未实现物理**：保守决策，不运行物理模拟；接收参数或记录幅度不算物理实现。
- **编辑器证据**：必须有可追溯的“仅用于编辑/导出、不参与运行时”正面证据才能归类。
  本次没有为下列尚未消费的键取得这种充分证据，不能因键名或参考实现没用就归此类。
- **未知不能判定**：用途或完整语义证据不足；当前未专门消费既不是“编辑器专用”，
  也不是“可安全忽略”。`EmoteModel::Load` 保存原始 `PsbDocument`，只解析选定字段，
  并未对未知 metadata 建立运行时兼容保证。

## 2. E10：逐键判定表

本表逐项覆盖主计划列举的控制键及其子字段，并补列已实现的 `selectorControl`、
`attrcomp` 与 `metadata.mirror`。相似名称不合并：`mirrorControl` 不等于 `mirror`，
`immediateControl` 不等于 `instantVariableList`，通用插值/夹取也不等于对应 Control。

| 键 / 路径 | 判定 | 当前代码证据及确切边界 | 尚缺的证据 / 处理 |
|---|---|---|---|
| `transitionControl` | **official-runtime-static-confirmed；已接入（有限）** | SO `InitTransitionControl` `0x66d41c` 消费 enabled/label，单维过渡；项目 `emote_control` 解析并经 `BuildEmoteControlRegistryPlan` 登记，`EmotePlayer::Load` 为启用的 label 建立 base `Animated`，禁用 label 不登记；`emote_control_player_regressions` 合成断言。 | 默认值、时间/ease 与 timeline/selector 覆盖顺序的官方动态对照仍缺；不能以通用 fade 代证。 |
| `selectorControl` | **已实现运行时（有限）** | `EmoteModel::Load` 解析 label、enabled、optionList；`EmotePlayer::ApplySelectors` 在 `ComposeVariables` 最后覆盖 option 变量，`SelectorOptionValue` 对小数选择值交叉淡化。 | 项目合成行为可定位；真实服装/表情及官方覆盖顺序仍待验证。 |
| `immediateControl` | **official-runtime-static-confirmed；已接入（有限）** | SO `0x66e8b8` 消费 enabled/label，直写 type9；不等于 instantVariableList。缺此键才走 `InitDefaultImmediate` `0x66eef8`，显式空表不 default。项目 `BuildEmoteControlRegistryPlan` 计算 authored + 缺键默认的 instant 集合，`WriteBaseVariable` 对 immediate label 无时长、不排队直写；缺键 / 显式空 / 显式表三条路径有合成断言。 | 重复 label 与禁用优先级为本地安全策略；官方动态对照仍缺。 |
| `clampControl` | **official-runtime-static-confirmed；project-support-separate（未实现，降级加载）** | SO Init `0x66f9c8` / Reflect `0x6784c4` 消费 type,var_lr,var_ud,min,max；normal branch 的 type1 为 pair 圆投影约束，type0 仍未知。项目 `EmoteModel::Load` 对启用的 clamp 记录 `clampControl.type0/1` 并不应用，模型仍加载；通用 scalar/selector clamp 不是此算法。 | P1-B：补投影坐标归一、退化范围/执行顺序及 type0 分支证据；禁止先当普通 scalar clamp 实现。 |
| `mirrorControl` | **部分静态用途已知；mirror 取负已实现，其余特殊算法未知** | `EmoteModel::Load` 解析 mirrorControl.variableMatchList；`EmotePlayer::ComposeVariables` 在有效镜像且 `MirrorMatches`（大小写敏感字节子串，空项匹配全部）时对整个合成值取负，getter 不变；`emote_mirror_regressions` 合成断言。 | 与 eye/selector/blink 的覆盖顺序为本地策略未证；stereovision 未做；真实匹配模型对照缺；不由 E11 代结项。 |
| `eyeControl` | **已实现运行时（自动眨眼子集）** | `EmoteModel::Load` 解析 label、beginFrame/endFrame、enabled 与 blink 字段；`EmotePlayer::AdvanceBlinks/ApplyBlinks` 驱动变量。 | 已实现不等于整个眼部控制协议完整；未知扩展字段及官方优先级仍需核验。 |
| `eyebrowControl` | **official-runtime-static-confirmed；project-support-separate（未实现）** | SO Init `0x66b8bc` / Progress `0x674088` 有 runtime edge/node 消费；完整算法未恢复。项目无此控制器，普通眉毛变量不代证。 | P1-B：先恢复 edge/node 插值、范围边界、缺省与覆盖顺序，再实现/回归；参考实现不驱动不代表编辑器专用。 |
| `mouthControl` | **official-runtime-static-confirmed；project-support-separate（未实现）** | SO Init `0x66c520` / Progress `0x674138`：label 输入转 mouth int，talkLabel 为 talk float，由 SetVariable 驱动，不是直接读取 audio；项目无专用消费。 | P1-A：补整数转换/值域、talk 混合与闭口/边界规则后实现变量驱动控制器；不虚构直接音频同步。 |
| `bustControl` | **明确未实现物理** | 无此表解析；`EmotePlayer::SetBustScale` 仅保存非负幅度并说明 physics is not simulated；Lua `m_setOuterForce/m_setOuterRot` 明确返回不支持。 | 保持 E5 保守决策；表内完整字段/公式仍未知，不声称其所有语义均已恢复。 |
| `hairControl` | **明确未实现物理** | 无此表解析；`EmotePlayer::SetHairScale` 仅保存幅度并记录未模拟；外力代理走 `EmotePhysicsUnsupported`。 | 无头发弹簧/阻尼/风场逐帧求解；幅度 knob 不是物理支持。 |
| `partsControl` | **明确未实现物理** | 无此表解析；`m_setOuterForce` 校验 part=`parts` 后返回 `false, reason`，没有对应求解器。 | 外力接口形状不证明坐标轴、单位或完整控制表；按需另立物理契约。 |
| `orbitControl` | **official-runtime-static-confirmed；已接入（有限）** | SO Init `0x671738` / Progress `0x673a24` 消费 interval/tween/orbitFrameList，周期派生变量；tween 使用当前 segment 剩余时间。项目 `ProgressEmoteOrbit` 经 `EmotePlayer::ProgressOrbits` 在 Progress 末尾写 base 变量（首次不消费 dt、同 index 不重发、duration=(index+1)*(T/N)-phase）。 | interval/tween 缺省为必填的本地策略；官方动态对照缺。 |
| `charaProfile` | **official-runtime-static-confirmed；已接入（C++ 查询）** | SO `0x6714ec` 构建 runtime query 的 height/pixelMarker map，不是每帧 render 控制器；项目 `EmoteControlConfig` 解析，`EmoteModel::Height()`/`FindPixelMarker()` 提供纯 C++ 查询（未暴露 Lua）。 | 坐标/单位/生命周期与是否需要 Lua 接口的需求证据仍缺。 |
| `variableMatchList` | **official-runtime-static-confirmed（匹配子路径）；mirror 匹配已实现** | driver-contract §4 已有运行时匹配判定；项目解析 mirrorControl.variableMatchList 并用于 mirror 取负。普通 `SetVariableDiff/GetVariableDiff` ordered pair 已实现。 | 真实匹配模型、反序/共享 label 对照仍需；不能叫 pair 白名单；stereovision 名单未消费。 |
| `selectorControl[].optionList` / `optionList` | **已实现运行时（仅 selector 上下文）** | `EmoteModel::Load` 为每项建立 `EmoteSelectorItem` 并引入目标变量；空列表拒绝；`ApplySelectors` 按项下发。 | 不推定同名字段在其他控制表中也受支持。 |
| `selectorControl[].optionList[].offValue` / `offValue` | **已实现运行时（仅 selector 上下文）** | `EmoteModel::Load` 读取，缺省 0；`SelectorOptionValue` 使用 `on + (off-on)*min(abs(v-index),1)`。 | 独立字段或其他上下文未知；不是全局开关协议。 |
| `selectorControl[].optionList[].onValue` / `onValue` | **已实现运行时（仅 selector 上下文）** | `EmoteModel::Load` 读取，缺省 1；同一交叉淡化公式在选中整数项给 on。 | 与 offValue 同样限定消费位置；完整官方边界仍待证。 |
| `eyeControl[].blinkIntervalMin` | **已实现运行时** | `EmoteModel::Load` 归一到非负；`EmotePlayer::ScheduleBlink` 以 LCG 在区间内选等待帧数。 | 官方随机分布/种子一致性未证明；测试可固定 min=max。 |
| `eyeControl[].blinkIntervalMax` | **已实现运行时** | 模型加载时归一到至少 min；`ScheduleBlink` 使用归一后的 max。 | 同上，项目策略不是官方随机性证明。 |
| `eyeControl[].blinkFrameCount` | **已实现运行时** | 模型加载时至少 1；`AdvanceBlinks` 按 40% 闭、20% 保持、40% 睁推进并跨相位消耗步长。 | 相位实现有合成断言；不声称所有模型观感与官方一致。 |
| `eyeControl[].blinkEnabled` | **已实现运行时** | `EmoteModel::Load` 缺省启用；`AdvanceBlinks` 与 enabled 一起门控控制器。 | 此键不代表 Lua 动态眨眼配置协议已暴露。 |
| `talkLabel` | **official-runtime-static-confirmed；project-support-separate（未实现）** | mouth Init/Progress `0x66c520/0x674138` 将其作为 SetVariable 驱动的 talk float 输入，和 label 的 mouth int 分开；项目无专用消费。 | P1-A：随 mouth 排期，先补 talk 输入范围/过渡/关闭边界，不能称直接 audio callback。 |
| `face_cheek` | **official-runtime-static-confirmed（精确默认变量名）；已接入缺键默认** | `InitDefaultImmediate` `0x66eef8` 在 immediateControl 键缺失时加入默认变量名及 instant 集合；显式空表不 default。项目缺键默认路径已实现（见 immediateControl），被已登记 label 占用时跳过。 | 不是 metadata 任意 face_* 通配；官方动态对照缺。 |
| `face_tears` | **official-runtime-static-confirmed（精确默认变量名）；已接入缺键默认** | 同 `0x66eef8` 默认变量/instant 集合，键缺失才启用；项目随 immediateControl 缺键路径实现。 | 缺失/显式空/显式配置三条路径有合成断言；官方动态对照缺。 |
| `face_eye_hi` | **official-runtime-static-confirmed（精确默认变量名）；已接入缺键默认** | 同 `0x66eef8` 精确名字；项目随 immediateControl 缺键路径实现，不用 eyeControl 代证。 | 不扩成任意高光/face 前缀协议；官方动态对照缺。 |
| 其他 `face_*` | **未知不能判定** | 没有按 `face_` 前缀解释 metadata 的代码；通用 label 处理不是通配协议。 | 必须按实际出现的字段逐项补证，三个已知名字不代表完整集合。 |
| `metadata.mirror`（补充，非 `mirrorControl`） | **已实现运行时（三态）** | `EmoteModel::Load` → `Mirrored()` → `EmotePlayer::Load` 记录 metadata 镜像；有效镜像 = metadata XOR 用户状态，`SetMirror` 赋值用户状态（非 toggle），`Render` 下发 reversex 为有效值。 | E11 的模型镜像能力不能证明 mirrorControl；`setMirror` 是项目扩展。 |
| `attrcomp`（补充） | **已实现运行时（remove 子集）** | `EmoteModel::Load` 收集 value<=0 的 remove 规则；场景 `Validate` 与节点求值均跳过匹配节点/子树。value>0 当前直接略过。 | 不声称其余 attrcomp 操作、正值规则语义已实现。 |

### 已有合成证据与 E10 未达项

- `tests/emote_regressions.cpp` 的 `PlayerTests` 包含 eyeControl 周期、
  selector 公式/默认项、attrcomp 移除与 metadata.mirror 断言。
- `tests/emote_timeline_regressions.cpp` 的 `BlinkClockTests` 包含并行控制器、
  大步长、enable 门控、加载重置与动画等待不受眨眼阻塞等断言；
  二者随 `build-test` 27/27 通过（本机合成回归）。
- `tests/emote_control_regressions.cpp`、`tests/emote_control_player_regressions.cpp`
  覆盖 transition / immediate（缺键 / 显式空 / 显式表）/ profile / orbit 的 core 与 player 接入；
  `tests/emote_mirror_regressions.cpp` 覆盖 mirror 三态与取负。
- **E10 不能以“眨眼/选择器/attrcomp 三项已做”概括为全部完成。** 当前逐键表
  已建立，normal consumers 静态事实已将多项归属从 unknown 更正为官方运行时；完整算法
  及项目实现仍分开，未证明编辑器专用的项不能如此忽略。物理明确不实现；clamp（type0 未知 /
  type1 圆投影）、stereo、eye edge、eyebrow nodes、mouth shape 等尚未实现，不能静默消项。

### E10 明确排期与实现前置证据

以下是主 E10“运行时项有实现或明确排期”的执行安排，不是动态兼容完成声明，不给虚构
日历日期。A/B 表示依赖顺序，仍属 P1；算法边界未恢复前先补证据与合成反例，不猜实现。
**当前进度**：A1 transition、A2 immediate/默认名、B3 orbit、P2 profile 已接入 model/player
（合成回归通过，官方动态对照缺）；B4 中 mirror 三态与 variableMatchList 取负已实现；
A3 mouth/talk、B1 clamp、B2 eyebrow、stereo、eye edge 尚未实现。

| 优先级 / 工作包 | 排期范围 | 实现前必须补的证据 / 出口 |
|---|---|---|
| P1-A1（已接入） | transitionControl 单维控制 | 类型/默认值、SetVariable 路由、时间/ease、与 timeline/selector 的覆盖顺序；有初始化及过渡端点/中点夹具后实现 |
| P1-A2（已接入） | immediateControl/type9 与三精确默认名字 | 缺键与显式空的区别已知；补 type9 写回、enable/重复 label 优先级及 instant 集合用途，三路径夹具先行，不拿 instantVariableList 替代 |
| P1-A3 | mouthControl/talkLabel | 已证变量驱动；补 mouth int 转换/范围、talk float 混合与闭口规则，先测整数/小数/边界/过渡；不假设直接音频输入 |
| P1-B1 | clampControl pair（type1 与未知 type0） | type1 圆投影不简化 scalar；补归一/退化 min=max/输出组合顺序，恢复 type0 或明确拒绝范围后才实现 |
| P1-B2 | eyebrowControl edge/node | runtime 归属已知但完整算法未恢复；需 edge/node 布局、采样/端点/空数据/覆盖顺序及对照轨迹，再写求值器 |
| P1-B3（已接入） | orbitControl | 补周期/segment 选择、时间单位、interval 缺省、大步长跨段；tween 按当前 segment 剩余时间，测试循环边界后实现 |
| P1-B4（部分：mirror 三态 + 取负已实现） | mirrorControl / variableMatchList 特殊路径 | 先恢复匹配后的反射、ordered pair 最终混合/优先级；parse 名单和普通 pair/getter 不代表此路径；补反序/共用 label 夹具及真实模型 |
| P2（C++ 查询已接入） | charaProfile 查询 | 恢复 height/pixelMarker 查询签名、坐标/单位/生命周期，确认自有 API 需求后实现查询；不当每帧 render 功能 |
| P1 调查；P3 按需 | 其余 face_* 与物理控制 | 其余前缀需逐个实际字段消费者，不把三个默认名泛化；bust/hair/parts 保持 E5 A，若转物理实现先取得公式与真实逐帧需求/对照 |

已实现 blink/selector/attrcomp 继续保留合成、最后全构建及真实流程复核；上述排期只补
运行时缺项的明确后续，不取消主/detail 原真实验收、官方对照或 DoD。

## 3. E12：不向 Lua 暴露 `CalcLayerFrameInfo`

**决定：保留不暴露 Lua 的边界，不新增同名代理，不宣称恢复了 native 输出结构。**

- 官方静态证据见驱动契约 §3、§7、§8：存在 `CalcLayerFrameInfo` 记录名称与
  求值/回放入口；完整 C++ 签名、返回方式、逐层数组/对象字段、所有权与有效期
  **尚未恢复**。格式中的单个 `%f` 不足以证明时间单位或输出 ABI。
- 本项目由 `EmoteScene::Evaluate` 生成自己的 `EmoteSceneLayer` 数据，再经
  `EmoteScene::Render` 和 `EmotePlayer::Render` 驱动合成器。这是用途覆盖，
  不是与官方结构布局/生命周期兼容，也不是逐层结果已与官方等价。
- `src/script/lua_engine.cpp` 的 `kEmoteMethods` 没有该方法；未知名字会由
  `EmoteIndex` 返回 `EmoteStub` 并首见告警。能够取到一个 stub closure 不等于实现。
- 当前本仓库材料没有建立新增公开接口的需求；这不证明所有游戏均不调用。
  若出现真实调用，先取得调用参数/返回使用证据，再决定自有稳定查询接口或适配层，
  不按猜测的 native 内存结构补字段。

E12 已有明确“不暴露”的产品决策及依据；**native 输出 ABI 恢复与动态差分仍未达成**，
不能将这一决策写成官方逐层帧信息协议已完整兼容。

## E15 / E16：官方 normal 静态事实与项目扩展边界

来源：上述固定 SO（hash/容器链见 driver-contract §1/§9）与本次静态研究 agent 交接。
这是 normal-path/注册表事实，不是运行游戏、设备、SO 或动态截图验证。

| 事实面 | 官方静态确认 | 项目支持 / 仍缺的证据 |
|---|---|---|
| Android Lua 注册 | `artemis::CEmoteSurface::RegisterLua 0x5509b0` 在本议题相关名字中只注册 isAnimating，无 skipToSync/contains/hitTest（不是整张方法表只有一个方法） | skipToSync 保持不注册是有依据的接口决策；contains/hitTest 统一stage为项目自有扩展，不能称官方Lua双API |
| native sync | `MMotionPlayer::SkipToSync 0x6ab794` 非loop路径查 motion.tag 的 type1、time>=current、content.sync；无命中 fallback lastTime | 不是递归非parameterize max；无 selfSyncTime 字段。旧 SyncFrame/C++ SkipToSync 明示 legacy extension；TagSync 只读 model 查询（`NextMotionSyncFrame`）已实现并有合成回归（emote_sync_regressions），Lua 不注册 |
| sync wait / animation | `IsSyncWaiting 0x6abb24` / Release `0x6abb2c` 为独立eventwait；IsAnimating仍看controls/playingtimelines | 不把sync wait合并成动画结束三来源优先级；需独立查询/释放边界、loop/命中/fallback合成证据与真实流程 |
| native geometry | `Contains 0x675d5c` 为shape type1/geometry查询 | 无官方Lua hitTest/contains区别或统一stage协议证据；项目full-affine/shapeleaf及定向通过仅证明自有路径，非mask像素/官方坐标等价 |

主/detail 原验收条文保留，旁注明确“三来源结束优先级”参考前提已被官方正面事实推翻，
不以“缺证据”口吻继续照搬。真实对话不挂起/不提前、几何边界与目标需求对照仍需完成；
不能以合成回归、静态注册结论或 TagSync 查询已实现代替这些动态验收。

## 4. E17：按需求门槛暂缓序列化

**决定：暂不增加 `serialize/unserialize`。这是按需工作尚未启动，不是已证明所有游戏
都无恢复需求，也不是已完成表情/timeline/blend 的读档一致性验收。**

### 自有存档 / onLoad 链的源码证据

无需提取或复制商业脚本即可审阅以下本仓库链路：

1. `LuaEngine::SaveSnapshot` 要求有效槽位与 `onSave`，先调用该 callback，
   再将非 g./s./t. 的变量银行交给 `EncodeVariableBank(..., true, ...)`，
   最后经 `WriteSaveFile` 原子写出；系统银行另存。
2. `src/util/save_storage.cpp` 的 `EncodeVariableBank/DecodeVariableBank`
   定义自有 ARCV v1、长度与 CRC 校验。它序列化的是键值银行，
   不遍历 `emotes_` 或 `EmotePlayer` 的私有播放状态，不是 VM/官方 E-mote 快照。
3. `LuaEngine::LoadSnapshot` 要求 `onLoad`；先解码 ARCV 或 BOWS、校验值图与
   允许的原生图层命令，再恢复槽位变量、清空队列/等待/音视频与 `emotes_`，
   丢弃 runner 旧 flow，释放并重建 compositor，回放允许的图层命令，
   最后进入 RebuildDerived 阶段调用 `onLoad`。
4. 因此 E-mote 只能由 restorer 根据保存的逻辑数据重新创建并设置，
   **引擎本身不自动保证重建到读档前同一播放帧**。`EmotePlayer::Load`
   使用新 player，重置变量/眨眼状态；播放列表、排队项、base frame 与 blend
   等私有数据没有被上述银行编码器自动存取。
5. `tests/runtime_regressions.cpp` 有合成 native 导入必须有 restorer、callback
   在变量恢复后执行一次、旧队列/等待被清空，以及独立 ARCV 槽位 onSave/onLoad
   闭环断言。这些证明链路设计可测试，**没有断言 E-mote 表情/时间轴/blend
   保存重建等价**；本任务也未运行这些测试。

### 启动门槛及仍需的恢复证据

- 门槛：取得有权审阅的实际调用/存档格式事实，证明脚本调用对应接口，或证明
  现有 onLoad 逻辑数据不足以恢复所需立绘状态。没有取得该证据只能叫“暂缓”，
  不能写作“明确无需求”。历史某个存档可回读也不能排除其他标题/槽位的需求。
- 可先用自有合成模型与自行编写的 onSave/onLoad callback 验证重建能力，无需
  商业脚本副本；本次文档任务不新增代码/tests。至少还需覆盖：
  - **表情/表达变量**：直接变量、selector 小数选项与覆盖顺序在重建后保持；
  - **timeline**：当前标签集合、flags、位置、循环/hold-end/完成态及排队项；
  - **blend**：当前比例、淡入淡出目标、剩余时间/ease 和归零移除行为，
    不能只存一个静态比例；
  - 自动/手动 progress、重建后的首帧及下一帧是否出现重复推进/跳帧；
  - 若需求包含精确眨眼相位，另定义等待/相位/随机状态是否保存，不默认承诺。
- 合成状态/像素断言证明自有链的能力后，仍需针对目标需求做经授权的官方同状态
  对照；表情、时间轴与 blend 的恢复各有证据才可验收，不能由“读档 callback 成功”
  代替。若最终决定无需原样播放恢复，应记录具体宿主/标题/场景边界与依据。

## 5. README 声明边界与交付未达项

- 加密不是“全不支持”：当前 `DecodePsb` 有 v2–v4 加密头、v2 指定 body 区间、
  seed 设置与 MDF/LZ4 frame 路径；v2 无头校验，错误 seed 与坏 body 无法可靠区分。
  Adler32/CRC 不提供密码学认证。官方 `EmoteFilterTexture` 的纹理 callback
  与结构过滤边界另见驱动契约，不能称其为已完成的任意整包解密/过滤器替代。
- 模板/stencil/阴影不是“一概拒绝”或“全部支持”：场景遍历已接受部分容器节点，
  并有 stencil 标签传播与 stage alpha mask 合成；这不构成任意模板参数实例化、
  全部 stencil 模式或独立阴影/光照模型。旧日志“masks not implemented”已改为
  “stencil containers present …（仅解释 stencilType 位 0x4）”；判断有限实现仍应
  同时看 `EmoteScene::Render` 与 compositor。
- **E2/E3** 已有合成回归，但官方模型/轨迹对照缺，`PlayingTimelineFlagsAt` 与官方 `|1` 的差异未改；
  **E7** 多文件仍拒绝；**E9** 降级项、模板/曲线/粒子与官方视觉边界未闭环；
  **E14** 有标题运行观察与 Mac 读档复现，但立绘/对话流畅性及设备/官方同状态矩阵未补；**E16** 几何
  命中（quad/mesh/shape）+ 三角形容差修复、stage full-affine/shape leaf 已落地并定向通过，不等于 mask 像素命中或真实
  游戏坐标/遮挡官方一致。本文不将这些项标为完成。
- E10 的逐键证据表不消除未知，E12 不暴露不消除 native ABI 未知，
  E17 暂缓不等于无需求。主/detail/驱动契约/格式证据的顶部账本与本文同步到同一时点。
- 本文只同步文档，无提交、暂存、发布或设备/游戏存档操作；构建与测试结果按“当前时点同步”
  所列时点单列，不冒充真实游戏、Android 设备/JNI 或官方一致验收。
