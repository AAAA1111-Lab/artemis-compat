# E-mote 格式与行为证据（Z / 网格 / 组合 / 同步位）

> 2026-10-16 当前时点。只记行为事实与 VA/hash；不含反编译代码、商业控制点、
> 标题密钥或商业模型。事实来源是固定官方 Android 样本的静态研究交接，加本项目
> 合成夹具的观察；本文不运行游戏、设备或官方库。当前本机合成与构建时点：`build-test` 27/27，
> `build-cgl` 29/29，Android 12 个入口导出检查通过（不代表游戏/设备/官方一致）。
> 静态确认不等于动态视觉一致，项目是否实现另列。

## 1. 样本身份与 VA 口径

| 项目 | 值 |
|---|---|
| 样本 | `arm64-v8a/libartemis.so`，9292144 bytes |
| SHA-256 | `cb2b58d8080b29529b27a8453f602646ab5f13ae5d4acc59b673c27c27e2d9cc` |
| 外层容器链 | 见 [driver-contract](emote-driver-contract.md) §1（APK / plugin ZIP 长度与 SHA-256） |
| SO 来源 | `TyranorNext-1.42.apk` → `assets/nativeplugins/artemis.zip!/arm64-v8a/libartemis.so`；VA 为 ELF 静态 VA |
| DLL 样本 | `/Users/weiss/Desktop/emote-test/emotedriver.dll`，SHA-256 `40d2e510106fc8a79e0fd07e5c0d4839d75503089d6fc9de0f0904f0ab73bd4d`，基址 `0x10000000` |

- 标注 `SO` 的 VA 只属于上述固定 SO（ELF 静态 VA），标注 `DLL` 的只属于上述
  `emotedriver.dll`（基址 `0x10000000` 的 VA）；两者不能互相混用，也不能与其他 PE 槽表地址混用。
- 本文第 2–6 节的行为事实，凡后续调查已给出地址的，已回填到“VA”列；**未获得地址的行仍写
  “待补录”**，不据记忆或相邻函数推测地址。补录须来自可重复读取的静态结果。
- 地址区间标明的是承载该行为的代码范围，不等于逐条验证区间内每条指令；静态确认不等于动态一致。
- 已在 driver-contract / 能力证据中持久化的 VA 原样引用（第 7 节）。mirror 三个函数的
  新旧地址经 `.dynsym` 核对，只是“入口”与“内部证据范围”的粒度不同，并非冲突（第 7、8 节）。

## 2. 有效 Z 数据流

| 事实 | VA |
|---|---|
| 运行时有效 Z 取自逐层累计的 `coord[2]`（沿父链累加后的结果） | 待补录 |
| icon 的 `metadata.zorder` 只是静态交叉校验值，运行时不用它排序 | 待补录 |

- 项目侧：Z 通道已用累计 coordZ 实现，已移除按标签的 hack。coordZ 与 icon metadata
  在一批模型上全图吻合只是**限定观察**，不是官方同状态验收。
- `metadata.zorder` 与运行时 Z 不一致不能据此判断任一方出错；以累计 `coord[2]` 为准。

## 3. rawMeshList 布局与 neutralIndex

| 事实 | VA |
|---|---|
| `rawMeshList` 是资源引用；内容为 4x4 float32 little-endian 网格 | 待补录 |
| 每个 mesh 恰 128 字节（单元内部排布见下）；总长必须等于 `meshCount * 128` | DLL：128 字节步长采样与 32 分量逐分量线性插值 `0x1005b69b–0x1005b749`、`0x1005bb32–0x1005bb93`、`0x10059270`。总长校验本身无独立地址，仍待补录 |
| 第一条为绝对网格，之后每条为相对前一条的 delta（累计得到各 mesh 的绝对值） | DLL：运行时重建与差分 `0x1005bd20–0x1005bd99`；列表求和 `0x1005b9b0`；分量加减 `0x10059480`、`0x100595c0` |
| `neutralIndex = -1` 合法，仅作“无中性项”标志，不作数组下标 | 待补录 |

- 第一行（`rawMeshList` 是资源引用、float32 little-endian）没有独立地址，保持待补录；
  上面 128 字节步长/32 分量的地址只支持“每 mesh 128 字节、32 个分量”这一布局。
- 非 -1 的 `neutralIndex` 须落在 `[0, meshCount-1]`，越界视为坏数据。

### 单元内部排布

- 每个 mesh 为 32 个 float（128 字节）：4x4 控制点，**行优先**；每个控制点的 x、y **交错**存放
  （`x0 y0 x1 y1 …`）；x 随列变化，y 随行变化。
- neutral（中性）首控制器为单位网格：`I_{j,i} = (i/3, j/3)`，`i` 为列、`j` 为行，各取 0..3。
  DLL 单位网格 `0x1002e700`。
- 以上排布由研究交接给出；`0x1002e700` 是单位网格的地址，逐点公式与行/列轴向没有另给独立地址，
  不在此推测。与已有文档（`emote-plan.md`、本节上方表格）的“4x4 float32LE、128 字节/mesh”
  无冲突，只是补上此前“不在此展开”的内部排布。
- 项目侧 `emote_combinator.{h,cpp}` 按上述布局校验长度与范围；该单元为数学 core，
  预算为总 64 MiB / 单项 8 MiB / 4096 条，standalone ASAN 通过。这是单元证据，
  不是整场景接入证据。

## 4. meshCombine 与 combinator

| 事实 | VA |
|---|---|
| `meshCombine` 沿父链向上查找来源，遇到停止条件即止（不穿过继承屏障） | DLL：shape 资格与屏障 `0x10044031–0x10044188`；祖先合并 `0x100442c1–0x10044368`；域建立 `0x1004436a–0x100445c3`；链头剔除 `0x10044311–0x10044328`（与祖先合并区间重叠，原样记录） |
| `meshCombinator` 优先于 frame 级 `bp`：存在 combinator 时 `bp` 不参与 | DLL：combinator 接管标记 `0x1005b138–0x1005b153`、`0x1005bdc5–0x1005bde0`；帧插值避让 `0x10042971–0x10042a04`；单帧拷贝避让 `0x10042cd2–0x10042d0b` |
| combinator 全部 neutral 时结果仍是 combinator 的输出，**不回退**到 frame `bp` | 待补录（上行接管/避让地址未单独标明此“全 neutral”分支，不借用） |

- 非 head 的 shape anchor 补偿见第 6 节；combinator 自身的采样/插值/重建地址见第 3 节。

- 项目侧 `emote_scene.cpp`：combinator 已接入 scene（Load 时解码并受预算约束），同域
  meshCombine 已实现，combinator 优先于 frame `bp`。对无法证明的 combine 组合（低位同步通道、
  cc、屏障、异域、动态/退化祖先证明、运行期精确检查失败等）**降级加载**：该节点按“无 combine”处理
  （保持自身网格，不输出猜测值），并以 `UnsupportedFeatures` 记录
  `meshCombine.fallback.{lowSync,cc,barrier,domain,dynamic,runtime}`；这不是已支持。
- 真实 132 个 fg 模型 DecodePsb / Model / Player Load 全部成功（仅加载级），每个模型都命中
  `clampControl.type0/1`、`meshSync.lowChannels`、`meshCombine.fallback`，131 个有 `timeline.openEnded`；
  `EmoteScene::Load` 经图集解码缓存由约 4.2s 降到约 50ms。这是加载级观察，不是渲染或官方对照。
- ancestor 停止规则的完整形式化仍在研究。

## 5. 低位 sync 与 shape 位

| 事实 | VA |
|---|---|
| 同步掩码的低位通道（coord/angle/zoom 一类）与 shape 位是不同的位域，不可合并判断 | DLL：low sync `0x10042e60–0x1004330c`；shape 位判定见 shape 资格 `0x10044031–0x10044188`；nested 传递 `0x10046528–0x100465a0`、`0x100488a0–0x100488f4`。“两者不可合并”这一结论无独立地址，由上述区间的静态阅读得出 |

- 判定“是否带 shape 作用域”只看 shape 位，不看低位 sync 通道；反之亦然。
- 项目侧 meshSync：有限形状作用域已实现，无 frame 时挂起；低位通道（`meshSync.lowChannels`）
  不应用并记录，各 sync 通道的完整语义不属于已实现范围。

## 6. 未完成：屏障 anchor 补偿

- 继承屏障处的 anchor 补偿**尚未完成**。项目对该情形降级加载：该节点的 combine 不应用并记录
  `meshCombine.fallback.barrier`（`emote_scene.cpp` 的 `CombinePreflight::Prove`），
  不输出猜测补偿值。
- 恢复补偿前不得在文档或代码里把 meshCombine 称为支持屏障场景。
- 官方侧屏障 anchor 补偿的承载代码：DLL `0x100449d3–0x10044ca1`。这只是已定位的地址，
  补偿公式未在此记录，项目仍未实现。

## 7. 已持久化的相关 VA（原样引用）

| 对象 | VA | 出处 |
|---|---|---|
| `InitMirrorState` | `0x6690b0` | driver-contract §4（新调查范围 `0x6690b0–0x6690e4`，起点一致） |
| `SetMirror` | `0x67d484` | driver-contract §4（新调查范围 `0x67d484–0x67d4bc`，起点一致） |
| `InitMirrorControl` | 入口 `0x66fdd4` size 468 | driver-contract §4（已核对：入口 VA；内部证据范围 `0x66fe08–0x66fe5c`，粒度不同，非冲突，见第 8 节） |
| `MatchMirrorVariable` | 入口 `0x677864` size 2628 | driver-contract §4（已核对：入口 VA；内部证据范围 `0x677930–0x6779b8`，粒度不同，非冲突，见第 8 节） |
| `ReflectVariableValue` | 入口 `0x674594` size 200 | 入口来自 `.dynsym`；内部证据范围 `0x6745c0–0x674620`。早期记录的 `0x6745fc..0x674620` 是内部范围、来源未确认，不是函数地址（见第 8 节） |
| orbit Init / Progress | `0x671738` / `0x673a24` | 能力证据 §2（`Progress` 与第 9 节一致） |
| `RegisterLua` | `0x5509b0` | 能力证据 E15/E16 |

## 8. Mirror（SO，新调查地址）

| 事实 | VA |
|---|---|
| `Init` | SO `0x662ca4–0x662ca8` |
| `InitPhysics` 读 metadata | SO `0x666168–0x6661a8` |
| `SetMirror` | SO `0x67d484–0x67d4bc` |
| `Mirror` getter | SO `0x67d4c0` |
| `InitMirrorState` | SO `0x6690b0–0x6690e4` |
| `InitMirrorControl` | SO `0x66fe08–0x66fe5c` |
| `MatchMirrorVariable` | SO `0x677930–0x6779b8`；`memchr` 调用 `0x677974`，`memcmp` 调用 `0x677988` |
| `ReflectVariableValue` | SO `0x6745c0–0x674620`；取负 `0x674610` |
| mirror 条件 XOR 证据 | DLL `0x1000aad5–0x1000aae6` |

- **地址冲突：已核对：粒度不同，非冲突。** 入口 VA + size 取自统一 SO
  （`TyranorNext-1.42.apk` → `artemis.zip` → `arm64-v8a/libartemis.so`，SHA-256
  `cb2b58d8080b29529b27a8453f602646ab5f13ae5d4acc59b673c27c27e2d9cc`）的 `.dynsym`；
  上表为各函数体内部的证据范围。两者须分列，不得合并成一个值：

  | 符号 | 入口 VA | size | 内部证据范围 |
  |---|---|---|---|
  | `InitMirrorControl` | `0x66fdd4` | 468 | `0x66fe08–0x66fe5c` |
  | `MatchMirrorVariable` | `0x677864` | 2628 | `0x677930–0x6779b8`（`memchr` `0x677974`、`memcmp` `0x677988`） |
  | `ReflectVariableValue` | `0x674594` | 200 | `0x6745c0–0x674620`（取负 `0x674610`） |

- 早期记录的 `ReflectVariableValue` = `0x6745fc..0x674620` 是内部范围、来源未确认，不是函数地址；
  现以入口表为准。
- `memchr`/`memcmp` 地址与能力证据中“大小写敏感的字节子串匹配、空项匹配全部变量”的描述相符，
  但本文不据此新增结论。

## 9. Orbit / Immediate / Default（SO）

| 事实 | VA |
|---|---|
| `ProgressOrbitControl` | SO `0x673a24` |
| interval 等分 | SO `0x673a68–0x673ab0` |
| 首次 Progress（不消费 dt） | SO `0x673a44–0x673a64` |
| 同 index 跳过（不重发） | SO `0x673ab4–0x673ab8` |
| tween | SO `0x673ac4–0x673ae8` |
| duration = 0 | SO `0x673afc` |
| `PEmotePlayer::Progress` | SO `0x65fd3c` |
| `CalcLayerFrameInfo` | SO `0x6728cc` |
| `_CalcLayerFrameInfo` | SO `0x6733c0–0x673400` |
| `InitImmediateControl` | SO `0x66e8b8` |
| `InitDefaultImmediateControl` | SO `0x66eef8`；名称表 `0x8bea10` |
| `InitPhysics` 缺键分支（immediateControl 键缺失走默认） | SO `0x666448–0x666490` |
| `InitPhysics` 控制器注册顺序 | SO `0x666368–0x666490` |

- `ProgressOrbitControl` `0x673a24`、`InitDefaultImmediate` `0x66eef8`、`InitImmediateControl`
  `0x66e8b8`、`CalcLayerFrameInfo` `0x6728cc`、`PEmotePlayer::Progress` `0x65fd3c`
  与能力证据/driver-contract 既有值一致。
- 地址与行为的对应取自调查交接；行为语义（首次不消费 dt、同 index 不重发、tween 取剩余时间）
  见能力证据“当前补充”，此处只补地址。

## 10. 边界

- 本文是格式/行为事实，不是实现完成声明；E9（Z / meshSync / combine）、E16 均未关闭。
- 官方内核同状态逐像素对照（手机）进行中，0138 对照、Android 宿主合同验收未完成，见主计划账本。
