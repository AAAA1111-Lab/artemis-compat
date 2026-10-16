# E-mote 详细实施计划（参考实现对照落地）

## 全计划执行 / 验收账本（2026-10-16，当前时点）

执行目标仍覆盖 **E1–E17**，计划未全部完成。完整逐 E 的 `implemented / synthetic / real /
staticdecision / missing` 账本与验证时点见[主计划顶部](emote-plan.md#execution-ledger)。
本节只勾选限定代码/决策子项，**不勾选需要官方测试的验收**。下文原设计、历史记录、
DoD 与各 WP 验收均保留，不因本次汇总删约束；历史“已落地”也不意味着整项完成。

- [x] E1 `staticdecision/synthetic`：120 行逐槽表、240 DWORD 独立校验通过；mirror 三函数地址已核对为入口 vs 内部范围粒度不同；完整 ABI 未知。
- [ ] E2 `implemented/synthetic/staticdecision/missing`：ordered pair 控制器（second 聚合、getter 分离、有限性）、mirror 三态与 variableMatchList 取负已实现；官方模型对照、与 eye/selector/blink 覆盖顺序、stereovision 未验。
- [ ] E3/WP0 `implemented/synthetic/staticdecision/missing`：flags bit0 清全部、bit1 私有差分（非 sequential）、Step/Skip/Pass 按官方静态语义、独立 setQueuing/getQueuing 已实现；`PlayingTimelineFlagsAt` 仍返回 stored flags（官方为 `|1`，未改）；官方/插件轨迹缺。
- [x] E4 `implemented/synthetic`：分数灰度与 CGL 像素断言（build-cgl 29/29，compositor 回归连续 3 次）；窗口/设备观感另验。
- [x] E5 `implemented/synthetic/staticdecision`：A 分支专用 setOuterForce/setOuterRot 校验 + unsupported 已实现；不是物理模拟。
- [ ] E6/WP6 `implemented/synthetic/staticdecision/missing`：LZ4 校验加固已实现并有合成坏流用例；真实 v2 body 样本缺。
- [ ] E7 `staticdecision/missing`：多文件明确拒绝；拆分样本、合并规则与一致渲染未闭环。
- [ ] E8/WP7 `implemented/synthetic/missing`：桌面格式/图集已做；移动端压缩格式无样本，色序/alpha 官方对照仍需。
- [ ] E9/WP5/WP10 `implemented/synthetic/real/missing`：有效 Z 通道（去 label hacks）、完整 affine + shape leaf、warped mask source、meshSync 有限作用域、meshCombinator core + 预算 + scene 接入、同域 meshCombine（combinator > bp）已实现；低位 sync / cc / 屏障 anchor 补偿 / 异域等降级加载并记录 UnsupportedFeatures；真实 132 个 fg 模型 Load 全部成功（仅加载级）；曲线/模板/粒子与官方逐像素对照未完成，不结项。
- [ ] E10/WP1–WP3 `implemented/synthetic/staticdecision/missing`：transition / immediate / profile / orbit 已接入 model/player，mirror 三态已对齐，多 blink 时钟已修；clamp / stereo / eye edge / eyebrow nodes / mouth shape 未实现。
- [x] E11/WP4 `implemented/synthetic`：tint/metadata.mirror 与 GL 像素断言；不是 mirrorControl/官方整体兼容证明。
- [x] E12 `staticdecision`：不暴露 Lua；native 输出结构/ABI 未恢复，保留未知。
- [x] E13 `staticdecision`：可选项不启动（无 Windows 运行环境）；没有真实轨迹 diff 通过。
- [ ] E14 `implemented/synthetic/real/missing`：隔离采集工具（save-dir/frames/timeout/completion）与 Mac 读档复现路径已就绪；标题 1200 帧 exit0 无立绘；newgame 2600 因取名 modal 超时；官方同状态逐像素对照（手机）进行中，0138 未闭环。
- [ ] E15/WP8 `implemented/synthetic/staticdecision/missing`：TagSync 只读查询（NextMotionSyncFrame）已实现，Lua 不注册（官方样本无 skipToSync）；旧 SyncFrame/SkipToSync 为项目 legacy 扩展；真实等待/同步流程未验。
- [ ] E16/WP9 `implemented/synthetic/missing`：几何命中（quad/mesh/shape）+ 三角形容差修复、stage full-affine/shape leaf 已落地；contains/hitTest 为项目扩展（官方 Lua 只有 isAnimating）；官方点击/遮挡未验收。
- [x] E17/WP11 `staticdecision`：按需延期决策；ARCV/onLoad 重建安全网不含 player 状态；不是证明所有游戏不需要。
- [ ] Android 宿主合同（官方壳与 Tyranor-Next 各自 Activity/插件）验收；插件重新构建发布（TyranorNext 内 `libartemis-clean.so` 仍是旧 v29，需更新 manifest `pluginVersion`）。
- [ ] 最后并行修改稳定后重跑原 DoD/主 §E14 的宿主验收（真实 GL 游戏、Android 设备/JNI）；不复用旧 22/22、CGL 24/24 等记录。

**验证时点**（本机合成与构建，不代表游戏/设备/官方一致）：`build-test` 27/27，`build-cgl` 29/29
（compositor 回归连续 3 次通过），`build-mac` / `build-android` / `build-consumer` /
`build-consumer-android` 构建通过，Android 12 个入口导出检查通过，`git diff --check` 通过。
Mac 不能运行 Android 交叉产物，导出/链接通过不等于 JNI 行为兼容。更早的 22/22、23/23、
24/24、25/25 属此前时点，不与当前混用。

**当前补充**（并存于上文，不删原验收）：格式/行为证据见
[emote-format-evidence.md](emote-format-evidence.md)（Z 取累计 `coord[2]`、rawMeshList
128 字节/mesh 首条绝对后续 delta、`neutralIndex=-1` 仅作标志、combinator 优先于 frame
`bp` 且全 neutral 不回退、低位 sync 与 shape 位区分）；屏障 anchor 补偿未恢复。
WP1–WP3 相关：mirror 有效值 = metadata XOR 用户状态，`Mirror` getter 返回用户状态，
匹配为大小写敏感字节子串、空项匹配全部，官方样本无 Lua `setMirror`，项目 `setMirror`
为扩展。orbit：首次 Progress 不消费 dt，整周期回同 index 不重发，
tween duration=`(index+1)*(T/N)-phase`。stage 蒙版缓存缺陷已修（左右眼共用蒙版源、
跨 owner key 碰撞，改按 owner + 解析源集合），Mac 读档 say_6/shi_36 眼睛虹膜/高光/红晕恢复。
真实游戏快照中未观察到眨眼（不判定存在与否）。

**仍缺**：官方内核同状态逐像素对照（手机，进行中）、0138 闭环、Android 宿主合同验收、
插件重新构建发布；E6 真实 v2 body 样本、E7 拆分样本、E8 移动端压缩格式样本、E17 需求门槛，
E13 不启动。所有未达项回流账本，不以决策/暂缓/标题页截图替代原验收。无提交、暂存、发布或设备存档操作授权。

> **定位**：`docs/emote-plan.md`（主计划，E1–E17）的执行细化。本文覆盖
> 「可落到代码与夹具」的工作包（WP0–WP11），每项包含：目标、现状证据、
> 设计、改动文件、实施步骤、回归夹具、验收标准、风险。
>
> **参考实现（只取行为/接口事实，不复制代码，AGENT.md clean-room 约束）**：
> - `art3m1s-core`（`/Users/weiss/github- engine/art3m1s-core`，Artemis 生产宿主）：
>   `crates/asb-interpreter/src/lua_engine.rs`（**Artemis Lua 契约**）、
>   `crates/art3m1s-emote`（Rust 实现）、`src/runtime/emote.rs`（集成/眨眼）。
> - `krkrsdl3`（KiriKiri Z，BSD 三条款式）：`plugins/emoteplayer`。
> - `crates/eluna`（MPL-2.0，外部 `xmoezzz/eluna`）：实验后端。
> - **实测游戏脚本**：甜蜜女友3 `system/image/emote.lua`（经 `artc` 从
>   root.pfs 提取到临时目录审阅；游戏资产不进入仓库）。
>
> **完成定义（DoD，所有 WP 通用）**：
> 1. `build-test` 全绿 + 新增回归；2. `build-mac` / `build-android` 编译通过；
> 3. 不改变既有 Lua 行为（除本 WP 声明的）；4. 契约来源写进代码注释；
> 5. 涉及渲染的项在 mac 宿主（有真机则加真机）冒烟并记录。
> 验证命令见主计划 §E14。

## 0. 批次划分

| 批次 | 工作包 | 前置条件 |
|---|---|---|
| **P0（先做，证据已闭环）** | WP0 Artemis Lua 契约对齐（单位/签名/表参数/新方法） | 无需新素材 |
| **A（立即可做，合成夹具可闭环）** | WP1 自动眨眼、WP2 选择器、WP3 attrcomp、WP4 模型镜像位、WP6 PSB 加固、WP7 图集裁切 | 无 |
| **B（需真实模型/真机确认）** | WP8 sync/skipToSync、WP9 命中测试、WP5 帧曲线与颜色、WP10 网格扩展 | 样例模型 + 实测游戏 |
| **C（按需）** | WP11 serialize、物理（主计划 E5） | 游戏需求证据 |

映射：WP0 → E3/E10；WP1/WP2/WP3 → E10；WP4 → E11；WP5/WP10 → E9；WP6 → E6；
WP7 → E8；WP8 → E15；WP9 → E16；WP11 → E17。

## 历史进度快照（当前状态以顶部账本为准）

> 原进度表保留历史事实；“E5/E12/E13/E17 决策完成”只说明所述决策，不能代替
> 动态验收或证明所有游戏无需求。WP9/E1/WP6 等“已落地/完成”仅限当时子集，
> E9 的新 Z/combine、E16 一般 affine/shapeholder 与最新全构建不得据此勾选。

| WP | 状态 | 备注 |
|---|---|---|
| WP0 | **已落地（合成回归通过）** | 帧单位、setCoord/setScale/表参数/progress 选项/新 knobs/setColor RGBA；PlayTimeline flags 留给 E3；真机回归留 E14 |
| WP1 自动眨眼 | **已落地** | 40/20/40 相位模型、等待不计动画、重载重置 |
| WP2 选择器 | **已落地** | 交叉淡化公式 + 渲染断言 + selector 变量可写 |
| WP3 attrcomp | **已落地** | value<=0 移除、Validate/Evaluate 同步跳过 |
| WP4 模型镜像位 | **已落地** | metadata.mirror 默认生效，Lua 可覆盖 |
| WP6 PSB 加固 | **已落地** | v2 body 解密、宿主 seed 钩子、lzfs LZ4 帧（独立块） |
| WP7 图集裁切 | **已落地** | win/common 共享图集 + left/top 裁切 |
| WP11/E11 颜色 tint | **已落地** | MODULATE2X（0x80 中性）→ colormultiply |
| WP5 曲线/帧色 | **帧色已落地**：`color`（MODULATE2X，2× 折算后与播放器 tint 相乘）；曲线字段仍只接受不应用（真实模型无 `zcc/ccc/cc` 命中） | |
| WP8 sync/skipToSync | **部分**：推导 + C++ SkipToSync + 回归；Lua 注册待真机证据 | |
| WP9 命中测试 | **已落地** | shape/blank/clip 源、rect/circle/point/quad、contains/hitTest |
| WP10 网格细分 | **部分**：细分比率接通网格输出（默认 1.0 不变）；priority 已接到 Draw；meshSync 已实现有限形状作用域、待更充分回归，早期“无 bp/cc 载荷结项”已撤回；owner 层序返修及独立复审通过，P1 在限定合成/静态边界可关闭 | 见 WP10 当前修订及主计划 §E9 |
| E1 契约文档 | **完成**：`docs/emote-driver-contract.md` | |
| E5/E12/E13/E17 | **决策完成**（见主计划「落地状态」） | |
| E14 真机冒烟 | **部分完成（历史验证）**：host 冒烟、macOS CGL GL 回归及 TyranorNext 立绘验证曾通过；本机 `artemis-mac --tap/--snapshot` 曾记录存/读档及脸部改善，不能替代当前源码与官方精确 A/B 验收 | 本轮测试边界见主计划 §E9 |
| 真实模型对齐（甜蜜女友3） | **已落地**：win-split 内容、motion 级 parameterize、stencil 子树、深度忽略、blank `w:h:ox:oy`、per-frame color、priority、`bm` 混合、盒中心原点、HOLD 子节点、stencil 蒙版合成；合成回归覆盖 | |
| E8 桌面贴图格式 | **已落地**：DXT1/DXT3/16bit/A8L8/RGBX8 + mip 容错（合成回归） | |
| 实机/本机历史修复 | **历史落地记录**：`Animated::Finish` 归零修复、inheritMask 放宽、bm 混合、盒中心偏移、stencil 子树/HOLD 递归；后续 stencil 蒙版合成、纹理独立化及解析 O(n) 修复有本机验证记录，不等于均已获设备复验或最新源码验收 | 本轮状态见主计划 §E9 |

---

## WP0 — Artemis Lua 契约对齐（P0，先于一切）

**当前执行状态**：native bit1 非 sequential、Step 不调用 Progress 已证，当前源码已按此实现
（flags bit0=0 清全部、bit1 私有差分、Step/Skip/Pass 按官方静态语义、独立 setQueuing/getQueuing，
有合成回归）；旧“保留项目队列”的决定不再作为最终契约。`PlayingTimelineFlagsAt` 仍返回
stored flags（官方为 `|1`，未改）。下方历史现状/设计保留作追溯，官方映射与真机入口仍按
E3/E14 原验收，不据代码落地全勾。

**目标**：把 Lua 代理的签名、单位与参数形式对齐真实 Artemis 游戏脚本与
`art3m1s-core` 的引擎绑定；这是"实测游戏能正确显示"的前置条件。

### 证据（甜蜜女友3 `system/image/emote.lua`，共 1605 行）

- `ex.frametime = 16.666` —— 引擎 E-mote 为 60fps；
  **所有 transition/fade/progress 参数单位是「帧」，不是毫秒**：
  - `local fr = p.frame or p.time or init.fg_fade; if fr > 0 then fr = floor(fr / ex.frametime) end; em:setVariable(k, v, fr, es)`
  - `local tm = ...; tm = floor(tm / ex.frametime); em:fadeOutTimeline(as, tm, 0); em:fadeInTimeline(alw, tm, 0)`
  - `fr = es * 0.06  -- player->Progress(elapse * (60.0f / 1000.0f)); em:progress(fr)`
  - `em:setColor(no, 0, 0)`（颜色 + 帧 + ease）
- 签名（与 `art3m1s-core` 绑定一致）：
  - `em:setScale(sc, 0, 0)` → `setScale(scale, origin_x, origin_y)`（缩放 + 枢轴，
    **不是** time/ease）
  - `em:setCoord(sx, sy, 0, 0)` → `setCoord(x, y, z, angle)`（**不是** time/ease）
  - `em:setVariable(label, value, frames, easing)`、`fadeIn/fadeOutTimeline(label, frames, easing)`
  - `em:playTimeline(label, 1)` —— flags bit0=1 表示"保留现有时间轴"；0 时参考实现
    会先清空全部时间轴（`art3m1s-core play_model_timeline`）
  - `em:getVariable(label)`、`em:isTimelinePlaying(label)`、`em:stopTimeline(label)`、
    `em:pass()/step()/skip()`、`em:getEmoteVersion()`
  - `e:getEmoteLayer{ id=(id..".0"), next=true }` —— **表参数**（兼容现有字符串形式）
  - `e:createEmoteLayer{ id, files={px}, width, height, progress=(bool) }` ——
    `progress=true` = 引擎自动推进（脚本不调 `progress()`）；`false` = 脚本手动推进。
    `ex.progressos = { windows=true, ps4=true, switch=true }`（**android 未列 → 引擎自动**）
- 甜蜜女友3 还使用本项目没有注册的方法：
  - `em:setMeshDivisionRatio(ratio)` —— 网格细分倍率（性能档位 1.0/0.8/0.4…）
  - `em:setHairScale(scale)` / `em:setBustScale(scale)` —— 摇动幅度缩放（物理相关）

### 现状与差距

- `m_setVariable/m_setColor/m_fadeIn/m_fadeOut/m_progress` 把帧当毫秒 →
  过渡快 ~16.7 倍（`setVariable(fr=20)` 应为 333ms，当前 20ms）；
- `m_setScale` 把 (origin_x, origin_y) 当 (time, ease)；
- `m_setCoord` 把 (z, angle) 当 (time, ease)，旋转被丢弃；
- `l_getEmoteLayer` 只接受字符串，游戏用表 `{id=, next=}`；
- 未注册 `setMeshDivisionRatio/setHairScale/setBustScale`（走 stub）；
- `createEmoteLayer{progress=}` 未解析（Android 恰好与自动推进一致，但语义要落实）。

### 设计

1. **单位边界**：Lua 层把帧 → 毫秒（`frames * 1000.0/60.0`）后再调 C++；
   C++ API 保持毫秒（既有测试不动）。`progress(frames)` 同法换算。
2. **setScale**：改为 `(scale, origin_x, origin_y)`；`EmotePlayer` 增加枢轴字段，
   渲染时通过容器变换表达（`translate(-ox,-oy)`，与
   `art3m1s-core` 的 `Affine2::from_translation(-origin)` 等价）。
   旧的 4 参 per-axis 扩展如需保留，放到独立方法名或按参数个数严格区分
   （不得再与官方 3 参冲突）。
3. **setCoord**：改为 `(x, y, z, angle)`；z 在 2D 合成路径忽略（记录一次），
   angle 走现有旋转。`setRotate` 作为本项目扩展保留。
4. **getEmoteLayer**：同时接受字符串与 `{id=…, next=…}`；`next=true` 先返回同一
   实例（Artemis 的 pending/promotion 语义单列，见风险）。
5. **新方法**：注册 `setMeshDivisionRatio/setHairScale/setBustScale`；
   一期为"接受 + 参数校验 + 记录一次日志"（不假装物理/细分），二期再实现。
6. **progress 选项**：`EmotePlayer::SetAutoProgress(bool)`；`UpdateEmotes` 仅对
   auto-progress 的实例做墙钟推进；`progress=false` 的实例只响应显式
   `progress()`/`step()`（Windows/PS4/Switch 脚本路径）。Android 默认 true。
7. `setColor` 字节序待验证：游戏构造 `0xRRGGBBff`（RGB+不透明 alpha），与
   DLL 初始化值 `0x808080FF` 一致指向 **0xRRGGBBAA** 而非当前假设的
   0xAARRGGBB；用真机灰度/colortone 场景验证后修正。

### 实施步骤

1. 先加 Lua 端回归：把提取出的游戏调用序列写成合成脚本断言（不进游戏资产）。
2. 单位换算（setVariable/setColor/fadeIn/fadeOut/progress）。
3. setScale/setCoord 语义重构（含容器枢轴变换）+ mac 渲染冒烟。
4. getEmoteLayer 表参数；createEmoteLayer progress 解析 + auto-tick 开关。
5. 新方法注册（stub + 校验）；setColor 字节序验证与修正。

### 回归与验收

- `runtime_regressions`：新增"帧单位"用例（`setVariable(label, v, 60, 0)` 应产生
  ~1s 过渡）、表参数 `getEmoteLayer`、`progress` 选项停止自动推进、
  `setScale/setCoord` 参数不干扰过渡时间。
- `emote_regressions`：枢轴变换几何断言；帧→毫秒换算数值断言。
- 真机：甜蜜女友3 立绘缩放/位置/头部转身/淡入淡出速度与官方一致。

### 风险

- `next=true` 的 pending/promotion 语义（转场时旧实例保留、新实例接管）需要
  与 `trans` 联调；一期可先返回现有实例并记录日志，不改变现有替换行为。
- setColor 字节序与 alpha 语义未闭环前不要改动画笔预设值。

---

## WP1 — 自动眨眼（eyeControl）

**最新执行补记**：多 blink 改为每个控制器独立消耗完整步长，跨相位不丢时间；
实现与扩展测试已并入当前 27/27 合成回归。当前随机为每 player LCG，
不是下方早期设计的 mt19937。现实现 ComposeVariables 为基础变量/时间轴 → blink →
selector；下方设计/风险对覆盖顺序曾不一致，保留作待核验约束，不能擅自声称官方优先级
已证或删掉真机自然眨眼、对话推进不挂起、wait 不超时验收。

**目标**：解析模型眨眼控制并自动播放，且**不影响动画结束判定**。

### 现状与证据

- `src/render/emote_model.cpp` 只解析 `variableList` / `instantVariableList` /
  `timelineControl`；眨眼、选择器、attrcomp 均未解析。
- krkrsdl3 `updateEyeControl`（较简）：随机等待 → 三角波闭开 → 恢复 `baseVal`。
- **art3m1s-core（Artemis 生产宿主，优先参照）** `EmoteEyeBlink`：
  - 解析 `enabled`/`blinkEnabled`（缺省 1）、`blinkFrameCount(max(1))`、
    `blinkIntervalMin/Max(max(0))`、`beginFrame`/`endFrame`；另有无运行时用途的
    `edge`/`node` 字段；
  - 状态机 `Idle → Closing → ClosedHold → Opening → Idle`，按原注释：
    **native EPEyeControl 相位为 40% 闭 / 20% 保持 / 40% 睁，运动相位速度
    2.5×（span*2.5/blinkFrameCount）**；
  - `apply(vars)`：仅当变量当前值落在 `[begin,end]` 时，
    `value' = base + (end - base) * amount`，`amount = (blink_frame - begin)/span`；
  - 等待时间：LCG（1664525/1013904223）在 `[min,max]` 上取值；
  - 大步长时要跨相位继续推进，避免眼睛卡在半闭。
- krkrsdl3 的真实事故：带眨眼/时间线控制的立绘若让 `animating` 恒真，等待
  动画结束的对话流程会永久挂起（猫娘乐园2）。

### 设计

- `EmoteModel`：
  ```cpp
  struct EmoteBlink { std::string variable;
                      double begin=0, end=0, frames=1;
                      double interval_min=0, interval_max=0;
                      bool enabled=true, blink_enabled=true; };
  std::vector<EmoteBlink> blinks_;           // Blinks()
  ```
  解析 `metadata.eyeControl[]`；校验 `label` 存在于 `variables_`（否则加载失败，
  保持"显式拒绝"风格）。
- `EmotePlayer` 每眨眼条目运行态 `{wait, blink_frame, phase, rng}`（`Load` 重建）；
  按 art3m1s-core 的相位/速度模型推进（tick=frames，ms 经
  `kFramesPerMillisecond` 换算）。
- 应用顺序对齐 art3m1s-core：时间轴采样 → 用户变量覆盖 → 眨眼最后应用；
  当前 `ComposeVariables` 以 `variables_` 为基准再加时间轴混合，接入时需保持
  既有 mixer 回归不变（新值只影响眨眼变量）。
- `IsAnimating()` **不含**眨眼（等待中的眨眼不算动画，避免挂起）。
- 随机：`std::mt19937`；测试用 `interval_min==interval_max` 保证确定性。

### 实施步骤

1. `emote_model.h/.cpp` 增加结构、解析、访问器与校验。
2. `emote_player.h/.cpp` 增加运行态、`Progress` 推进、`ComposeVariables` 合并。
3. 夹具：`tests/emote_scene_fixture.h` 新增 `BlinkDocument()`
   （`Scene()` + `eyeControl{expression, 0, 10, 4, 10, 10}`）。
4. 回归：`emote_regressions.cpp` 新增 PlayerTests 段。
5. 注释记录契约来源（krkrsdl3 `updateEyeControl` + DLL `blink*` 字符串）。

### 回归夹具与断言

- 等待期（未到 10 帧）：渲染后 `expression` 仍为 0（face 图标）。
- 第 10+2 帧（blink 中点）：`expression`=10（wide 图标），用现有 `picture_sum`
  断言宽度。
- `blinkFrameCount` 结束后：回到 base 值；再次等待。
- `Progress(5000)` 后 `IsAnimating()==false`（等待中的眨眼不得挂起动画判定）。

### 验收标准

- 合成夹具全过；真机立绘自然眨眼、对话推进不挂起、`[wait]` 不超时。

### 风险

- 眨眼变量若同时被时间轴/用户设置驱动，覆盖顺序需按参考实现（眨眼先、时间轴后）；
  用户 `setVariable` 为最高优先（写在 `variables_` 层）。

---

## E10 其余 runtime 控制项的实施排期

normal-path 静态消费者确认与项目支持分开：`official-runtime-static-confirmed` 不代表
`implemented` 或官方动态兼容。SO 样本身份/地址口径由 driver-contract 任务持久化；
本任务不编辑它。逐键地址、行为边界及证据前置详见
[能力证据表与排期](emote-capability-evidence.md#e10-明确排期与实现前置证据)。

| 优先级 / 顺序 | 项目待实现项 | 实现前置证据与合成出口 |
|---|---|---|
| P1-A1 | transitionControl | enabled/label 单维过渡已证；补默认值、变量路由、时钟/ease 和覆盖顺序，端点/中点夹具先行 |
| P1-A2 | immediateControl/type9、face_cheek/face_tears/face_eye_hi 默认协议 | 缺键 default、显式空不 default 已证；补直写/instant 集合消费与重复/禁用优先级，分别测缺/空/显式表；不得拿 instantVariableList 或任意 face_* 通配替代 |
| P1-A3 | mouthControl/talkLabel | label→mouth int、talkLabel→talk float 由 SetVariable 驱动已证；补转换/范围/talk 混合/闭口及过渡，不直接接 audio |
| P1-B1 | clampControl pair | type1 normal branch 圆投影已证，type0 未知；补 min/max 归一、退化范围/组合顺序，恢复 type0 或限定拒绝边界，不写成普通 scalar clamp |
| P1-B2 | eyebrowControl | edge/node runtime 消费已证；完整算法/布局/插值/边界/覆盖顺序待恢复，先有采样反例再实现 |
| P1-B3 | orbitControl | interval/tween/orbitFrameList 周期派生已证；补 segment/循环/单位/跨段大步长，tween 是当前 segment 剩余时间而非固定整段 |
| P1-B4 | mirrorControl/variableMatchList 特殊分支 | 普通 ordered pair/getter 与 parse 名单已做；特殊 mirror 算法、最终混合、反序/共用 label/优先级还需证据与夹具 |
| P2 | charaProfile runtime query | height/pixelMarker map 已证，不是每帧 render；先恢复查询 API、坐标/单位/返回生命周期及需求，再实现自有查询 |
| P1 调查 / P3 按需 | 其他 face_*、bust/hair/parts | 不泛化精确默认名；未知字段逐项找消费者。物理保持 E5 A，转实现须独立公式/需求及真实逐帧证据 |

以上排期满足“运行时项进入明确后续”的管理要求，**不满足动态兼容验收**。现有
blink/selector/attrcomp 仍须最后完整构建及真机流程；原 DoD、主 E10 判定表/实现或排期
要求、主 E14 矩阵与下方各 WP 验收不变。

## WP2 — 选择器（selectorControl）

**目标**：支持多选项切换变量组，且时间轴不覆盖选择器接管的变量。

### 现状与证据

- krkrsdl3 `emoteselect`：`selectorControl[].{label, optionList[].{label,
  offValue, onValue}}`；`selectValue(opt)` 选中项写 `onValue`、其余写
  `offValue`（**二值切换**）；加载时先 `selectValue(0)`；`updateTimelineControl`
  跳过所有 selector item 变量。
- **art3m1s-core（Artemis 生产宿主，优先参照）** `EmoteSelectorControl`：
  - 解析 `enabled`（缺省 1）与 `optionList`；
  - `apply(selector_value, vars)`：`opt` 夹到 `[0, n-1]`，每个 option 取
    `distance = min(|opt - index|, 1)`，写
    `on + (off - on) * distance` —— **相邻选项线性交叉淡化**（选中项精确为
    `onValue`，相邻项部分混合），而不是二值切换。
- 本项目未解析该元数据；`setVariable` 对未知变量返回 false。

### 设计

- `EmoteModel`：
  ```cpp
  struct EmoteSelectorItem { std::string label; double on=1, off=0; };
  struct EmoteSelector { std::string label; std::vector<EmoteSelectorItem> items;
                         bool enabled=true; };
  std::vector<EmoteSelector> selectors_;
  ```
  `variables_` 同时纳入 selector 自身 label（"虚拟变量"，值为当前选项）与所有
  item label（供 `SetVariable` 校验与时间轴跳过判定）。
- `EmotePlayer::SetVariable`：命中 selector label 时保存浮点选项值并按下发公式
  计算每个 item 的目标：优先采用 art3m1s-core 的交叉淡化（
  `on + (off-on)*min(|v-i|,1)`，支持小数/过渡）；krkrsdl3 的二值切换作为
  退化情况（整数取值）。越界夹取（对齐 art3m1s-core）而不是报错。
- `GetVariable(selector label)` 返回当前选项值（默认 0）。
- `ComposeVariables()`：时间轴采样跳过 label 属于任一 selector item 的轨道。
- Lua 无需新方法（`setVariable`/`getVariable` 覆盖）。

### 实施步骤

1. 模型解析 + 校验（`optionList` 空、label 重复、item label 不存在 → 加载失败）。
2. `EmotePlayer` 路由与状态；加载时执行 `select(0)` 初始化。
3. 夹具 `SelectorDocument()`：`Scene()` + `selectorControl{label='clothes',
   enabled=1, optionList=[{expression,on=10,off=0},{partner,on=10,off=0}]}`
   （partner 加入 `variableList`）。
4. 回归：`emote_regressions.cpp` + `runtime_regressions.cpp`（Lua
   `setVariable('clothes',0/0.5/1)` 后渲染/`getVariable` 断言交叉淡化数值）。
5. 时间轴跳过：用 `PlayerDocument()` + selector 包住 `expression`，断言时间轴
   播放不覆盖选择结果。

### 验收标准

- 夹具通过；真机服装/表情选项切换与官方一致；`setVariable(selector)` 越界报错。

### 风险

- "selector 变量被时间轴跳过"是参考实现的选择，需真机确认（若某游戏时间轴
  确实驱动 selector item，需要加白名单）。

---

## WP3 — attrcomp 节点移除

**目标**：按模型规则在加载期隐藏节点，与官方一致。

### 现状与证据

- krkrsdl3 `emoteattrcomp`：`metadata.attrcomp[].{label, data.remove[].{value,
  id:{chara, motion, layer}}}`；`value<=0` 时在加载期把目标节点标记
  `removed=true`（不渲染）。
- 本项目不解析该元数据 → 本应隐藏的层会显示。

### 设计

- `EmoteModel`：
  ```cpp
  struct EmoteRemoveRule { std::string chara, motion, layer; double value; };
  std::vector<EmoteRemoveRule> removals_;   // 仅收录 value<=0 的规则
  ```
- `EmoteScene::Evaluate` / `Validate`：遍历节点时对 `(chara, motion, node.label)`
  命中移除规则的节点直接跳过（含其子树）。被跳过节点内的不支持字段不得导致
  加载失败（Validate 同步跳过）。
- `value>0` 的规则：记录一次 `OutputLog` 并忽略（保持显式而非猜测）。

### 实施步骤

1. 模型解析；`EmoteScene::Load` 构建按 motion 索引的移除集合。
2. `Validate`/`Nodes` 跳过逻辑（两处必须一致，避免"能加载不能渲染"）。
3. 夹具 `AttrcompDocument()`：`Scene()` + `attrcomp` 移除 face 子节点。
4. 回归：断言层数/图标宽度变化；被移除节点带未知 content 字段仍可加载。

### 验收标准

- 夹具通过；真机隐藏层不出现。

### 风险

- 规则里的 `layer` 是节点 label；确认与 PSB 的 label 完全一致（大小写/空格），
  必要时先做 trim 兼容。

---

## WP4 — 模型镜像位（metadata.mirror）

**目标**：模型自带的镜像标志自动生效，Lua `setMirror` 仍可覆盖。

### 现状与证据

- krkrsdl3：`metadata.mirror==1` → `isMirror`，渲染矩阵乘 `scale(-1,1,1)`。
- 本项目只支持 Lua `setMirror`，不读元数据（`grep mirror src/render` 仅见
  玩家侧）。

### 设计

- `EmoteModel`：`bool mirror_` + `Mirror()`；解析 `metadata.mirror`（数值 1）。
- `EmotePlayer::Load`：`mirror_ = model->Mirror()`（失败重载时随新模型重置）。
- 渲染路径复用现有 `reversex` 容器属性，无需新代码。

### 实施步骤与回归

1. 模型解析与访问器；2. 夹具 `MirrorDocument()`；
3. 断言 `Render` 后容器层 `Layer::reverse_x==true`；`SetMirror(false)` 可覆盖。

### 验收标准

- 夹具通过；真机镜像模型朝向正确。

---

## WP5 — 帧曲线（zcc/ccc/cc）与 per-frame color（调查后实现）

**目标**：不因新格式字段拒绝加载；确认语义后应用数值。

### 现状与证据

- krkrsdl3 `emoteframe`：`zcc/ccc/cc`（缩放/坐标/网格曲线：2 系数 + 4+4 控制点），
  per-frame `color`（缺省 `0xff808080`，`hasColor` 区分"未写"）。
- **art3m1s-core**：已实现帧曲线求值（`render.rs` `curve_ratio(...)`，含
  coord/angle/zoom 通道插值）与 per-frame 颜色规范
  （`color_component` 支持 0..255 或 0..1，帧色缺省不乘）；其已知缺口里
  没有曲线，说明曲线路径已在真实游戏（NekoMiko）上验证。
- 本项目 `Validate` 的 supported 集合不含这些字段 → 直接抛
  `unsupported E-mote content`。

### 设计（分两步）

1. **兼容不拒绝**：解析字段（结构化保存），Validate 白名单；求值暂按"无曲线"
   处理，`OutputLog` 首见提示一次。
2. **应用数值**：以 art3m1s-core 的曲线/颜色实现为语义参照（曲线对
   coord/angle/zoom 的通道插值、帧色乘算），本项目实现并加数值单测；
   `color` 接 `colormultiply`（主计划 E11；帧色缺省不乘）。

### 实施步骤

1. 逐字段确认：先用真实模型统计哪些字段实际出现（决定优先级）。
2. 第 1 步落地 + 夹具 `CurveDocument()`（含三类曲线与 color）；
3. 第 2 步落地 + 数值断言（曲线端点/中点、color 乘法）。

### 验收标准

- 含曲线模型可加载；数值与参考实现一致（对照脚本或截图）。

### 风险

- 曲线语义属较新 PSB 版本，未见 Artemis 实测游戏使用；不要在无证据时投入
  第 2 步。

---

## WP6 — PSB 加固：seed 钩子 / v2 body / lzfs

**最新执行补记**：seed、v2 body 与 LZ4 路径已有实现，新增 LZ4 checksum/长度/边界
加固经静态 review 通过，最后全构建及真实 v2 body 模型仍缺。下方只解头部是立项快照；
v2 无校验，不把坏 seed/坏 body 伪装成可可靠区分。driver-contract 已更正 FilterTexture
callback 为纹理/mip pixel 缓冲，旧“固定 key 整包输出/强制解密”不得当作已证 API。
原坏流合成回归、真实加密模型加载及 README 同步要求全部保留。

**目标**：覆盖官方驱动的整包加密路径与 LZ4 容器（不含 `EmoteFilterTexture`
固定 key 的"强制解密"入口，后者单独评估）。

### 现状与证据

- `src/pack/psb.cpp`：仅解头部（`[8, header_len)`），seed 由 adler32 推导或
  `ARTC_EMOTE_SEED` 覆盖；PSB v2 无校验，失败信息不区分阶段。
- krkrsdl3 `emotefile::load`：
  - key = `{0x075BCD15, 0x159A55E5, 0x1F123BB5, seed}`；
  - 头部按固定字段顺序解密（v3+ checksum、v4+ extra 三个字段）；
  - **v2 额外解密 `[offsetEncrypt, offsetChunkOffsets)`**；
  - 宿主可用 `setEmotePSBDecryptSeed` / `setEmotePSBDecryptFunc(buffer,len)`；
  - `lzfs`（magic `04 22 4D 18`）LZ4 帧容器（块大小/独立块/块校验/内容长度/字典标志）。

### 设计

- 解密 seed 优先级：**显式宿主钩子 > `ARTC_EMOTE_SEED` > 头部推导**。
  宿主钩子：`DecodePsb(bytes, doc, error, const PsbDecryptOptions* = nullptr)`
  或 `PackManager::SetPsbDecryptHook(std::function<bool(std::vector<uint8_t>&)>)`；
  先内部 API + CLI/env，Lua 暴露不在本期。
- v2：解析头后按 krkr 顺序继续解密 body 区间；`checksum_ok` 对 v2 恒真，
  因此 v2 的"是否加密"用 `encryption_flags&1` + 解密后可解析性判定。
- LZ4：实现最小 `lzfs` 帧解压（仅独立块 + 块校验），或引入许可兼容的单文件
  实现；先支持必要特性，遇到字典/链接块明确报错。
- 错误分级：`"encrypted PSB: header key mismatch"` / `"... body decrypt failed"` /
  `"unknown PSB container"`。

### 实施步骤

1. `psb.cpp` 重构解密流程为"字段顺序表 + 可选 body 区间"，保持现有 v3/v4
   行为不变（先补回归再改）。
2. 增加 v2 分支与合成夹具（v2 头 + body 加密）。
3. 宿主钩子 API + `ARTC_EMOTE_SEED` 优先级测试。
4. `lzfs` 解压 + 合成 LZ4 帧夹具（手工构造小帧）。
5. `runtime_regressions.cpp`：加密 v2 模型经 `createEmoteLayer` 全链路。

### 验收标准

- `psb_encrypted_regressions` 全绿（v2/v3/v4、seed、坏流拒绝）；
- 真实加密模型可加载；README 能力表更新。

### 风险

- `EmoteFilterTexture` 的固定 key `851083516` 是"驱动自带强制解密"路径，是否
  需要由实测游戏决定；不要与 seed 路径混用。

---

## WP7 — 图集裁切（win/common spec）

**目标**：`win`/`common` 模型的共享图集 + 图标裁切可正确解码。

### 现状与证据

- krkrsdl3：非 krkr 时 `source.<name>.texture{width,height,type,pixel}` 是共享
  图集；每个 icon `{left,top,width,height,originX,originY}` 是裁切矩形；
  `readIconTobuffer` 从图集 `(left,top)` 起按行拷贝 `width×height`。
  （图标另有 `clip{left,top,right,bottom}` 字段，krkrsdl3 解析但未使用，语义待确认。）
- 本项目 `EmoteModel::Image` 只读 `icon.pixel` 并整图解析 → `win/common` 报
  `missing E-mote pixel resource` 或长度不符。

### 设计

- `Image()` 分支：
  - `krkr`：现状（每 icon 自带 pixel/pal/compress）。
  - `win`/`common`：读 `source.texture`（与 icon 同层）得到图集参数，裁切
    `left/top/width/height`；颜色序沿用现规则（win 交换 R/B，common 不换）。
- `EmoteImage` 保持输出 `width/height/origin_x/origin_y/rgba`，上层无感。

### 实施步骤与回归

1. 夹具 `AtlasDocument()`：2×2 图集 + 1×1 图标（left/top=1,0 等）验证裁切。
2. 断言 RGBA 像素、尺寸、origin；破坏性用例（越界 left/top 拒绝）。
3. 真机 `common`/`win` 模型冒烟。

### 验收标准

- 夹具通过；真实模型不再报 missing resource。

---

## WP8 — sync / skipToSync（验证优先）

**当前静态事实与项目边界**：固定 Android SO `RegisterLua 0x5509b0` 在相关名字中
只注册 isAnimating，没有 skipToSync/contains/hitTest；Lua 不注册是确认后的决策。
native `MMotionPlayer::SkipToSync 0x6ab794` 非loop查询 motion.tag 的
`type1/time>=current/content.sync`，未命中 fallback lastTime，不按递归非parameterize
最大值，没有 selfSyncTime 字段。`IsSyncWaiting/Release 0x6abb24/0x6abb2c` 是独立
eventwait，IsAnimating 仍看 controls/playing timelines。地址/样本 hash 引用见能力证据。

旧 `SyncFrame/C++ SkipToSync` 仅为 project legacy extension 的合成验证。
TagSync 只读 model 查询（`EmoteModel::NextMotionSyncFrame`）已实现并有合成回归，Lua 不注册；
真实等待/同步流程未验。下方两阶段旧设计与主原三来源条文保留，
但照搬三来源优先级的前提已被官方新事实推翻，不再以“有等待需求即可注册Lua”执行；
先按 native tag sync/eventwait 明确查询/状态边界，原真实流程不挂起/不提前要求保留。

**目标**：先建立 sync 事实，再决定是否接入结束判定。

### 现状与证据

- krkrsdl3：`syncTime` = 非 parameterize 节点最大内容帧（递归子 motion）；
  `selfSyncTime` 文件层；结束判定 `syncTime → selfSyncTime → lastTime`；
  `skipToSync()` 把时间跳到 `getSyncTime()`。
- 本项目：基础 motion 常驻推进，无 `play(motion)`/sync 概念；时间轴结束按
  `last_time`/`loop_end`。

### 设计（两步，默认只做第 1 步）

1. `EmoteModel` 计算并暴露 `SyncFrame(chara, motion)`（推导规则同上），
   仅在诊断日志使用，不改行为；增加 `EmotePlayer::SkipToSync()`（把
   `base_frame_` 跳到 sync 帧）但 **Lua 暂不注册**。
2. 真机确认 Artemis 游戏是否存在"等待立绘动画结束/跳到同步点"的流程；有则
   接入结束判定并注册 `skipToSync`，无则保持冻结。

### 回归与验收

- 合成 motion 推导 sync 值断言（含子 motion 递归、parameterize 排除）。
- 验收：第 1 步只要求数值正确；第 2 步需真机流程证据。

> 新事实旁注：保留上方原验收及下方原风险，但阶段二“注册Lua/结束三来源”不是现在
> 的执行许可。官方表不注册、tag查询与独立eventwait已证，需显式说明对旧参考方案的
> 替代边界，不能以旧数值测试或 TagSync 派发声称 native兼容/动态游戏通过。

### 风险

- 该语义来自 KiriKiri 侧插件，Artemis 引擎插件是否采用未证实；**不得**先改
  结束判定。

---

## WP9 — 命中测试：shape/blank 源

**当前执行状态**：几何/triangle 返修与 stage full-affine/shape leaf 已落地，静态 review
及合成定向通过。有限路径不等于全部仿射边界正确、mask 像素命中或官方坐标契约；
最后完整构建/GL、原点/边/顶点/缩放旋转及真机点击对照继续保留，不关闭整项。

固定 Android RegisterLua 不含 contains/hitTest，native `Contains 0x675d5c` 为shape
type1/geometry查询。项目 contains/hitTest 统一stage是自有扩展；下方参考实现的两套
坐标区别并未被证明是该官方Lua协议，不能宣称官方二者区别已实现。样本 hash 见能力证据。

**目标**：Lua `contains(label?, x, y)` / `hitTest(label?, x, y)` 与官方一致。

### 现状与证据

- krkrsdl3：`shape/<rect|circle|point|quad>`（单位方格 16×16，乘 zx/zy）、
  `blank:w:h:ox:oy`；`EmoteHitFrame::contains` 用网格三角形判定并按目标裁切；
  `contains` 保留旧仿射坐标，`hitTest` 用目标像素坐标。
- 本项目：`src` 仅接受 `src/…`/`motion/…`/`layout`，其余抛
  `unsupported E-mote source`；proxy 不注册 `contains`/`hitTest`（logging stub）。

### 设计

- `EmoteSceneLayer` 增加 `enum ShapeKind {None, Rect, Circle, Point, Quad}`、
  `shape` 字段与几何（顶点/半径/矩形）；`Evaluate` 收集 shape/blank 层
  （不绘制，`visible=false` 或单独输出）。
- `EmotePlayer::Contains(label?, x, y)`：无 label 时遍历全部 shape；有 label
  时命中该层。坐标空间=合成器 stage 像素（与输入层一致）。
- Lua：`contains([label,] x, y)` / `hitTest([label,] x, y)` 两种重载，
  返回 boolean。

### 实施步骤与回归

1. 场景解析 shape/blank（Validate 同步放开）；
2. 夹具 `ShapeDocument()`：rect/circle/point/quad 各一，含缩放/旋转；
3. 断言：中心/边/外点、旋转后判定、blank 尺寸/原点。
4. 真机点击判定冒烟。

### 验收标准

- 夹具通过；真机点击与官方一致（坐标对齐由引擎输入层验证）。

---

## WP10 — 网格扩展（调查）

**目标**：确认是否需要 `meshDivision` / `meshTransform` / `meshSyncChildMask`。

### 现状与证据

- krkrsdl3：节点带 `meshDivision`（运行时 CPU 细分缓存，`_meshDivX/Y`）、
  `meshCombine/meshTransform/meshSyncChildMask`；`priority` 列表重排 nodeList。
- **art3m1s-core**（语义参照最全，含 native 符号注释）：
  - `inheritMask`/`inheritParent` 走查、`motionIndependentLayerInherit`、
    `meshSyncChildMask` 低 3 位的 coord/angle/zoom 通道
    （注释标注 native `sub_10335500` 语义）；
  - `priority` 是**按时间变化**的排序表（`motion.priority_at(time)`，帧
    `content` 数组给出 rank），不是静态列表；
  - `stencilType & 0x4` 才启用 `stencilCompositeMaskLayerList`；节点 `type==1`
    为 shape 判定层；`bm` 每帧混合模式（0=alpha/1=add/2,5=rev-sub/3=multiply/4=screen）。
- 立项时本项目：固定 8×8 warped grid，不解析 priority/细分/混合模式，
  `bm!=0` 直接拒绝。此为历史差距，当前已接通细分倍率、priority 与 `bm`，
  不能再作为现状；剩余边界如下。

### 当前修订与验收缺口（2026-10-16）

> **当前时点更正**：本节以下保留早先 owner/mask 返修记录。Z 通道已落地并移除
> label hacks，获有限静态 review；coordZ/icon metadata 全图吻合不是官方同状态验收。
> meshSync red13→green 且无 frame suspend；meshCombinator 数学 core、总64 MiB/
> 单8 MiB/4096条预算及 standalone ASAN 已过，combinator 已接入 scene，同域 meshCombine
> （combinator 优先于 bp）已实现；低位 sync、cc、屏障 anchor 补偿、异域等降级加载并记录
> UnsupportedFeatures（不应用），真实 132 个 fg 模型 Load 全部成功（仅加载级）。
> stage full-affine/shape leaf 定向见 WP9。
> HIDE 已修精确 part，probe 已修为明确 production 顺序候选，仍不是最终像素归属；
> 以下“未修复”只适用于当时。当前验证时点见顶部（27/27、29/29），为合成与构建验证，
> 不是官方同状态验收。

- **meshSync**：早期“真实模型无 bp/cc 载荷、仅一次性日志即可结项”已撤回；
  后续发现帧级 4×4 Bezier 控制网格。当前按 `meshTransform` 与形状位建立
  有限形变作用域，结合 extent/继承挂起生成子部件网格；不等于完整支持各
  meshSync 通道、`cc` 或 meshCombine，仍需更充分回归（历史修订见主计划 §E9）。
- **层序返修完成，独立 review 确认 P1 可关闭**（限定合成运行/静态审查边界）。旧 `emote_tree` pairwise
  祖先排序在混合普通层时不满足严格弱序，全局 `paint` 可跨背景/效果组；
  本轮已替换为 owner 局部 `effect-path → paint → postorder rank → id` 全序，
  将 `priority` 接到 Draw 并新增合成回归。官方截图仍仅是局部视觉参考，完整
  变量/姿势未同步，不足以确立通用层序规则，不宣称真实游戏或官方等价。
- **owner 契约**：有效驻留且互不重叠；嵌套独立 owner 明确返回 `false`，不是支持。
  整体 owner/祖先 rename 映射 owner 并保留 rank/hint，同 owner 内 rename 保留；
  显式及 ordinary 附加成员跨 owner/移出时清除源 paint/rank/标记。冲突 rename
  返回 `false`、原子拒绝；delete 后旧 ID 重用无 ghost。`HitLayer` / `HitLayers`
  复用真实 Draw 候选顺序，仍为几何命中而非 mask 像素命中。
- **诊断证据边界**：`ARTC_EMOTE_PIXEL` 仅采样原图候选，忽略真实排序、mesh、
  mask、blend 等，不能证明最终像素归属；`ARTC_EMOTE_HIDE` 旧实现隐藏整个
  节点树而非纯图标。0138 差异仅有文字线索，缺精确对照及持久证据，不能
  断言官方被手/裙遮挡。probe/HIDE 限制本轮未修复。
- **蒙版回归**：新增双 mask-key、GL 释放/重建、非对称及 intermediate 覆盖
  已抓到 Y 翻转，修复为 `RenderStageMask` 的 `u_top_down=1`，mask 方向独立只读
  审查通过。早期 CGL repeat 3 次及修复前 22/22 baseline 保留为历史阶段记录。
- **效果状态**：最小修复显式回写 `grayscale=0`、normal 混合与白色 tint，
  此前 9 项失败已消除；新增固定 ID/图层保留回归，effect-reset 独立 review 已通过。
- **主 agent 独立返修后验证**：再次 `build-test` 重建 + ctest **22/22**；`build-cgl`
  全构建 + ctest **24/24**（含视频与 compositor）；更早合并版本 CGL compositor
  repeat **5 次**通过保留为阶段记录。
  `build-mac` 的 `artemis-mac` 与 `build-android` 构建成功，Android 导出脚本
  **12 入口**通过。末次 owner 尾项之后，主 agent 已独立重建 `build-consumer`，
  ctest **1/1** 通过；重建 `build-consumer-android`，无 JNI consumer final shared link
  成功，`git diff --check` 通过。Mac 不能运行 Android
  交叉产物。已有 warning 非新增错误，详见主计划 §E9「本轮审查状态与验收边界」。
- **边界与后续**：交付时索引状态以 `git status` 为准，暂存由主 agent 处理；
  本轮不提交、不发布、不复制 plugin、不改 manifest，v29 未经本轮验证；无游戏/手机
  运行，未触设备存档。仍需关注排序键构建祖先扫描性能、probe/HIDE 限制，并补受控
  本机游戏视觉/快照、手机官方状态对照、0138 证据及 Android 宿主实际合同验证；
  JNI 已知缺口见 `docs/embedding.md`，链接/导出通过不等于 JNI 行为兼容。

### 步骤

1. 真实模型统计字段出现率与观感影响（截图对比）；甜蜜女友3 脚本已确认游戏侧
   会调 `setMeshDivisionRatio`（性能档位），优先支持"接受参数 + 可配细分"。
2. 若需要：`emote_mesh` 支持可配细分（沿用 8×8 的顶点/索引构建），
   `EmoteScene` 按节点传递；priority 先按 `priority_at` 求值顺序实现，再对照
   截图验证绘制 z 序；`bm` 分帧混合按 art3m1s-core 的映射表接入合成器。

### 验收标准

- 有明确结论（做/不做）与证据；做则对照截图通过。

---

## WP11 — serialize / 物理（按需）

**当前决策分开记账**：E5 A 分支专用物理 unsupported 已实现，runtime 定向独立通过，
不运行物理；E17 仅按需延期，不是所有游戏均无需求。自有存档/onLoad 重建链见
[能力证据](emote-capability-evidence.md#4-e17按需求门槛暂缓序列化)，仍需表情/表达变量、
timeline、blend/剩余过渡恢复及限定目标的官方一致或明确无需求依据。

见主计划 E17（存档恢复）与 E5（物理，默认 A 显式不支持）。启动条件：
实测游戏脚本确实调用对应的 Lua 方法/存档需要恢复立绘状态。

---

## 里程碑

| 里程碑 | 内容 | 出口条件 |
|---|---|---|
| M1 | WP1+WP2+WP3（立绘正确性三件套） | 三平台构建 + ctest 全绿 + 真机立绘冒烟 |
| M2 | WP4+WP6+WP7（元数据/加密/图集） | 真实模型与加密样本可加载 |
| M3 | WP5+WP9+WP8 验证（新格式/命中/sync 结论） | 每项有实现或冻结结论 |
| M4 | WP10+WP11（按需） | 游戏需求证据驱动 |

## 风险总表

- **WP0**：单位为帧、签名按真实脚本/`art3m1s-core` 绑定对齐；`setColor` 字节序
  与 `getEmoteLayer{next}` 的转场语义未闭环前只做兼容读取，不改现有替换行为。
- **WP1**：眨眼与 `IsAnimating` 的耦合（参考实现出过挂起事故）——必须加
  "等待期不忙"回归。
- **WP2/WP3**：交叉淡化/隐藏语义来自 Artemis 侧参考实现，先按本文实现并用真机
  复核；发现冲突时以实测游戏为准。
- **WP6/WP7**：依赖样例文件；先收集加密/win-spec 模型再开工。
- **WP8/WP10**：语义未证实，禁止先改行为。
- 所有 WP 完成后同步更新 `docs/emote-plan.md` 状态与 README 能力表。

---

## 附录 A — 参考实现与证据来源

> 只做行为/接口核对，不复制代码；游戏资产与提取物不进仓库。

| 来源 | 路径 | 用途 | 已知缺口（同样值得参考优先级） |
|---|---|---|---|
| **Artemis 生产宿主** | `/Users/weiss/github- engine/art3m1s-core` | Lua 契约（`crates/asb-interpreter/src/lua_engine.rs`）、眨眼/选择器/diff 时机（`crates/art3m1s-emote`）、native 语义注释（`render.rs`，含 `sub_10335500` 等） | `pass/step/skip` 只记录不改变采样；stencil wipe/粒子/相机层；外部纹理 PSB；非 DXT5 图集；`cc` 网格 |
| **KiriKiri 实现** | `/Users/weiss/github- engine/krkrsdl3/plugins/emoteplayer` | 自动眨眼（较简）、选择器二值、attrcomp、sync、PSB seed/回调/v2 body、LZ4、图集裁切、网格细分 | 物理空实现；fade/blendRatio/pass/skip 为 TODO；插值无 easing；仅 RL/none + RGBA8 |
| **Eluna**（实验后端） | `art3m1s-core/crates/eluna`（外部 `xmoezzz/eluna`，MPL-2.0） | 第三方 PSB/渲染原语交叉验证 | 实验性 |
| **官方驱动** | `~/Desktop/emote-test/emotedriver.dll` | 18 项 native 方法契约（主计划附录 A/B/C） | x86 D3D9 闭源，仅本地分析 |
| **实测游戏脚本** | 甜蜜女友3 `system/image/emote.lua`（root.pfs 内，本机提取审阅） | WP0 单位/签名/方法面；眨眼/选择器等引用需求 | 资产不提交仓库 |

复现游戏脚本提取（只读，输出到临时目录）：

```bash
./build-host/artc list  "<游戏>/root.pfs" | grep -i emote
./build-host/artc ini   "<游戏>/root.pfs" "system\image\emote.lua" > /tmp/emote_scan/game_emote.lua
```

`art3m1s-core` 关键位置速查：

- 绑定：`crates/asb-interpreter/src/lua_engine.rs`（`EmoteLayerCommand`、`EmoteLayerApi`、
  `createEmoteLayer`/`getEmoteLayer`）
- 播放器：`crates/art3m1s-emote/src/player.rs`（`set_scale`/`set_coord`/`set_variable`/
  `play_model_timeline`/diff 时间轴时机）
- 时间轴与选择器/眨眼参数：`crates/art3m1s-emote/src/timeline.rs`
- 眨眼状态机：`src/runtime/emote.rs`（`EmoteEyeBlink`）
- 渲染语义：`crates/art3m1s-emote/src/render.rs`（inherit/meshSync/priority/曲线/混合）
