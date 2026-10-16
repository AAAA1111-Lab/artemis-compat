# E-mote 官方驱动静态契约与证据边界

> 授权兼容研究记录；只归档接口、格式和行为事实，不复制或改写第三方实现。
> 2026-10-16 重新读取下列本机 DLL 及 Android APK 内嵌插件库。未执行库、游戏、设备或存档操作；Android 库通过 ZIP 内存读取，未解包落盘，未读取外部实现源码。
> 本文及 `emote-driver-slots.csv` 不含二进制、反编译代码、标题密钥或商业模型。
> “静态已确认”不等于官方宿主 ABI 或真实模型动态兼容验证通过。

## 1. 样本身份、工具与证据等级

| 项目 | 直接读取结果 |
|---|---|
| 本机样本 | `/Users/weiss/Desktop/emote-test/emotedriver.dll` |
| 长度 | **1091072 bytes** |
| SHA-256 | **`40d2e510106fc8a79e0fd07e5c0d4839d75503089d6fc9de0f0904f0ab73bd4d`** |
| 格式 / preferred image base | PE32 DLL，x86 little-endian，Windows GUI；`0x10000000` |
| PE 时间戳 | 工具显示 2018-12-21 12:05:52；不是来源或工具链版本的真实性证明 |
| 导入 | 包含 `d3dx9_43.dll`；不能据此证明宿主已可运行 |
| 导出 | 13 项：三个 Emote 函数、两张接口虚表、接口构造/析构/赋值相关符号 |

文件哈希只固定研究对象，不证明发行来源或许可证。没有随样本取得新的来源凭据；本次授权仅用于提取兼容事实，不作实现移植。

### Android 后续参照样本（E2-A / E3-A）

| 容器 / entry | 长度（bytes） | SHA-256 |
|---|---:|---|
| `/Users/weiss/Desktop/TyranorNext稳定版/TyranorNext-1.42.apk` | 114704670 | `035349b12b8af648a1b8cd841ecd3b9b50f6c8ac880b33eb5fa8c538c7ea3e6d` |
| APK entry `assets/nativeplugins/artemis.zip` | 17126424 | `02bd818b3550551c3a309355f14da9f4108a3b35eeb7c37e2fcbf382763e897d` |
| ZIP entry `arm64-v8a/libartemis.so` | 9292144 | `cb2b58d8080b29529b27a8453f602646ab5f13ae5d4acc59b673c27c27e2d9cc` |

这是本机 TyranorNext 所捆绑的 Artemis 参照样本，ELF64 little-endian AArch64，含公开动态符号；**没有独立认证其上游官方发行来源、签名或版本对应关系**。以下 Android VA 只属于这个固定哈希，不能替换 Windows CSV 地址或套用其槽号。Android 公开方法符号是新增独立证据，不自动补全 Windows 未知正式名/完整签名；E1 CSV 仍保持原表及全部 `full_signature=unknown`。

- `r2` / `rabin2`：`/opt/homebrew/bin/`，radare2 6.1.8，darwin-arm64。
- Xcode `llvm-objdump` / `llvm-nm` 可由 `xcrun --find` 定位；`llvm-readobj` 未找到。
- PATH 未找到 `wine`、`wine64`、`wineserver`、`winedump` 或已检查的常见 Windows VM CLI。
- `/Users/weiss/Applications/CrossOver` 是空目录；存在旧 CrossOver bottle 目录不等于运行时可用。
- 指定样本目录只有该 DLL，未取得 E2 匹配变量模型、E6 整包加密模型或 E7 拆分 PSB。

本文置信度分开解释：**地址高**＝原始 DWORD/PE/RTTI 可重复读取；**名称高**＝包装入口直接转发到具有对应记录字符串的内部方法；**行为高（局部）**＝指定静态路径已核对。完整签名、全局语义、Lua 映射和动态 ABI 未证的地方仍为**未知/待验证**，不能从“名称高”外推。

## 2. E1：完整槽表、编号与映射边界

完整数据见 [`emote-driver-slots.csv`](emote-driver-slots.csv)：**120 条数据记录＋1 行表头**，即 player 95 条、device 25 条。不是 119 条，也不是将四张表分别相加的 240 条；每条记录并列保存一对同序 interface/implementation 指针。若同时排除两类的析构行，剩 **118** 条（94＋24），本 CSV 不采用该删行方式。

| 表 | VA | 文件偏移 | 指针项数（含析构） | 排除析构后的项数 |
|---|---|---|---:|---:|
| `IEmotePlayer` | `0x1008b900` | `0x8a700` | **95** | 94 |
| `PEmotePlayer` | `0x1008ba88` | `0x8a888` | **95** | 94 |
| `IEmoteDevice` | `0x1008b898` | `0x8a698` | **25** | 24 |
| `PEmoteDevice` | `0x1008bc08` | `0x8aa08` | **25** | 24 |

### 编号和 CSV 字段

- `slot0`：含析构的 **0-based 接口槽号**，player `0..94`、device `0..24`。
- `non_dtor0`：仅为本文定义的**非析构 0-based 序号**；slot 0 为 `NA`，其余等于 `slot0-1`。例如 SetCoord 为 slot 21 / 非析构序号 20。若采用非析构 1-based 序号，它恰等于 slot0，但不是地址偏移。
- `byte_offset`：相对各自虚表基址的字节偏移，`4*slot0`。该槽指针所在 VA＝表基址＋偏移。
- `interface_target_va` / `implementation_target_va`：文件原始 DWORD 所指的 preferred VA，**不是**指针所在 VA，也不是已重定位运行时地址。
- `internal_target_va`：只在已核对包装层转发时填写；其他为 `unknown`，不拿内部实现地址冒充虚表目标。
- `classification` 是已确认的粗分类；`unknown` 表示未分类，不按相邻槽推测。
- `method_name`：有记录字符串支持的名字；否则 `unknown`。`full_signature` 全部为 `unknown`：格式串只给记录参数形状，不足以恢复完整 C++ ABI、返回类型、所有参数类型和调用约定。
- `confidence`：`address_high`、`address_high;name_high` 或 `address_high;local_behavior_high`，其范围受上节限制。
- `evidence`：下述证据编号及内部地址。`mapping_status=static_index_only` 表示**同序静态并列**，不是动态接口调用验收。

### 表身份与边界（E1-T）

四张表前一 DWORD 指向 MSVC Complete Object Locator，其 RTTI 名分别为 `IEmotePlayer`、`PEmotePlayer`、`IEmoteDevice`、`PEmoteDevice`，object offset 均为 0。locator VA 分别为 `0x10095214`、`0x10094fc0`、`0x1009504c`、`0x10094fdc`。

- `IEmotePlayer[0]=0x10001330`；`[1..94]=0x10067fa3`（共同 pure-virtual 处理路径）。最后一项在 `0x1008ba78`；下一 DWORD `0x1008ba7c` 是下一类 `APSBObject` 的 RTTI locator 指针。
- `PEmotePlayer[94]=0x10003840`，所在 VA `0x1008bc00`；下一 DWORD `0x1008bc04` 是 `PEmoteDevice` 的 RTTI locator 指针。
- `IEmoteDevice[0]=0x10001260`；`[1..24]=0x10067fa3`；下一 DWORD 位于 `0x1008b8fc`。
- `PEmoteDevice[24]=0x100018d0`；下一 DWORD 位于 `0x1008bc6c`，指向 `AMotionPlayer` 的 RTTI locator。

因此旧记载“接口 player 94 槽、实现 player 95 槽”是混用了是否包含析构的计数，不是已证的接口增槽。

静态安装证据：工厂 `EmoteCreate`（`0x100038d0`）调用的构造路径在 `0x10001414` 安装 `PEmoteDevice` 表；device slot 9/10 进入共同构造 `0x10001f20`，其 `0x10001f5a` 安装 `PEmotePlayer` 表。**这些证据支持表身份和同序对照，但不证明未知槽的正式名称/签名、所有宿主版本的布局或 Artemis 插件的实际调用映射。** 接口非析构项都落到同一 purecall，不能从它们反推出名字。

其他分类证据：

- **E1-DTOR**：接口 slot 0 路径引用已导出的接口析构符号；实现 slot 0 执行清理及条件释放。归类 deleting-destructor，不补写编译器生成的正式方法名或签名。
- **E1-REF**：两表 slot 1/2/3 分别递增 `this+4`、递减并在零时经 slot 0 销毁、读回该计数。正式名字仍未知。
- **E1-DEVICE**：device slot 4 返回 `this+0xc`（不是“拷贝 getter”）；slot 9/10 分配 `0x24` 并进入上述构造。不能仅由此命名 CreatePlayer 或恢复输入格式。其他配置位含义未恢复。
- **E1-NAME**：CSV 中的 18 个命名包装入口已逐一核对直接分支目标，并与 §3 的记录字符串关联。名称不是导出的 C++ 方法符号。

## 3. 记录字符串能证明什么

下表 `f` / `s` / `d` 表示记录格式中的 `%f` / `%s` / `%d`，**不是完整 ABI 签名**。特别是 `%f` 不能独立证明原参数为 float/double，字符串不能独立证明时间单位或颜色布局。以往的 `time_ms` 和 `argb` 命名不再当作字符串已证事实；下文统一称 native duration。

| slot0 | 名称 | 记录参数形状 | 内部目标 VA | 格式串 VA |
|---:|---|---|---|---|
| 21 | SetCoord | f f f f | `0x100140d0` | `0x1008cf20` |
| 23 | SetScale | f f f | `0x10014280` | `0x1008cf38` |
| 25 | SetRot | f f f | `0x10014410` | `0x1008cf4c` |
| 27 | SetColor | d f f | `0x100145e0` | `0x1008cf5c` |
| 29 | SetGrayscale | f f f | `0x100147e0` | `0x1008cf70` |
| 40 | SetVariable | s f f f | `0x1000bf30` | `0x1008cd20`（引用点 `0x1000bfd2`） |
| 42 | SetVariableDiff | s s f f f | `0x1000d630` | `0x1008cd54` |
| 44 | SetOuterForce | s f f f f | `0x10014ca0` | `0x1008cf88` |
| 84 | SetOuterRot | f f f | `0x10015290` | `0x1008cfc0` |
| 57 | PlayTimeline | s d | `0x100171b0` | `0x1008d018` |
| 59 | StopTimeline | s | `0x10017500` | `0x1008d02c` |
| 60 | SetTimelineBlendRatio | s f f f d | `0x10017710` | `0x1008d03c` |
| 62 | FadeInTimeline | s f f | `0x10017a50` | `0x1008d064` |
| 63 | FadeOutTimeline | s f f | `0x10017bc0` | `0x1008d07c` |
| 65 | Skip | 无记录参数 | `0x1000b730` | `0x1008ccfc` |
| 66 | Pass | 无记录参数 | `0x1000bb10` | `0x1008cd0c` |
| 67 | Step | 无记录参数 | `0x1000b920` | `0x1008cd04` |
| 68 | CalcLayerFrameInfo | f | `0x1000b370` | `0x1008ccc4` |

## 4. E2：有序 pair 与 variableMatchList

**E2-PAIR（局部行为高）**：SetVariableDiff 内部 `0x1000d630` 构造两个字符串组成的 key，查找 `player+0x84` 的树。查找 `0x1001bd80` 调比较器 `0x1001eac0`；后者先比较第一个字符串，相等后在 `0x1001eb33..0x1001eb39` 比较 key `+0x18` 的第二个字符串。因此 key 是**有序二元组 `(label,pair)`**，不是旧文档声称的“只以第一个 label 为 key”。每个二元组有一个单值过渡控制器。

| 正常路径事实 | Windows VA | Android 公开方法 / VA |
|---|---|---|
| 有序 pair 对应独立单值控制器，初始值零；setter 不直接双写两个变量 | `0x1000d862..0x1000d941` | `MEmotePlayer::SetVariableDiff(const char*,const char*,float,float,float)` `0x678d68`；字符串引用重载 `0x679a58` |
| pair getter 未找到返回零；找到读当前控制值，不推进控制器，不返回最终渲染值 | slot 43 包装 `0x10002d70` → `0x1000d980`；`0x1000dabe..0x1000dadd` | `CEmotePlayerProxy::GetVariableDiff` `0x5526e8` → `PEmotePlayer::GetVariableDiff` `0x65f3ec` → `MEmotePlayer::VariableDiff(const char*,const char*)` `0x679c7c`；下游公开 `EPTransitionControl::epGet` |
| 每次更新清 pair 聚合表，推进全部 pair 控制值，按 key 的第二个字符串累加 | `0x1000dc50`，关键累加 `0x1000dcd3..0x1000dcf0` | `MEmotePlayer::ProgressVariableDiff(float)` `0x673ed0`，关键累加 `0x673f74..0x673f9c` |
| 基础变量值加 timeline difference，再加同 label 的 pair 聚合 | `0x1000c600..0x1000c713` | `MEmotePlayer::ReflectVariableValue()` `0x674594`；`AddTimelineDifference` `0x6776a4`；`AddVariableDifference` `0x677838` |
| mirror 匹配时翻转整个合成值，再交 motion 设置路径 | `0x1000c723..0x1000c841` | `MatchMirrorVariable` 入口 `0x677864` size 2628（内部证据 `0x677930–0x6779b8`）；`ReflectVariableValue` 入口 `0x674594` size 200（内部证据 `0x6745c0–0x674620`，取负 `0x674610`）；`SetMotionValue` `0x6782a8`。早期记录的 `0x6745fc..0x674620` 是内部范围、来源未确认，不是函数地址 |
| 普通 getter 优先读基础缓冲，缺项再走 motion 查询；不等于基础值加 pair 的渲染值 | `0x1000c4a0`，命中读回 `0x1000c583` | `MEmotePlayer::Variable(const char*)` `0x676fdc`；`SetVariableBuffer` `0x676fb8` |

Windows getter 的正式名在原 DLL 中仍未恢复，CSV 继续标 `unknown / pair_control_getter`；上表 Android 的同名公开 getter 是另一个样本的直接符号证据，不偷换其 ABI。

### 普通变量数据流公式（不是第三方实现代码）

令 `C[a,b](t)` 为该 ordered pair 当前控制值，`B[l](t)` 为基础变量缓冲，`T[l](t)` 为符合条件的 timeline 私有差分值乘 blend 后的和，则已核对的普通路径为：

| 量 | 接口/行为关系 |
|---|---|
| pair 聚合 | `D[l](t) = sum(C[a,b](t) for b == l)`；同 b、不同 a 可独立存在并相加 |
| 合成输入 | `U[l](t) = B[l](t) + T[l](t) + D[l](t)` |
| mirror 后输入 | 有效 mirror 且 label 匹配时 `V[l]=-U[l]`，否则 `V[l]=U[l]` |
| getter 分离 | pair getter 读 `C[a,b]`；普通变量 getter 优先读 `B[l]`，不默认读 `U[l]` 或 `V[l]` |

在上述求值链中，第一个字符串仅区分 pair 身份，**没有自动写 a 为 +v、b 为 -v，也没有按 v 正负改选目标 label**。pair setter 层未见将负 v 截为零。非正 duration 的控制命令立即采用目标；正 duration 的替换/排队还受独立 queuing 控制，见 §5。不能因此推断所有专用控制类型都走普通路径。

下游普通 motion 参数范围映射（Windows `0x10037920..0x10037a05`；Android 公开 `MMotionPlayer::SetVariableValue(Parameter*,float,unsigned int)` `0x6a664c`）：整数标志开启时先向零截断，再将值限制到端点 A/B 间；正常有限输入且 `A!=B`、division>0 时，frame 值为 `(clamp(value,min(A,B),max(A,B))-A)*division/(B-A)`，否则映射结果零。这是 **motion 参数层**的限制，不是 pair setter 层限制。

### E2-MATCH：名单真正父路径

| 父路径 / 用途 | 证据 |
|---|---|
| `metadata.mirrorControl.variableMatchList`：字符串列表参与 mirror 条件取负；不是 pair 二元白名单 | DLL 先取 metadata `0x10008111`，取 mirrorControl `0x1000ae94`，`0x1000aebb` 调 `0x10016390`；后者从该子对象取 variableMatchList。匹配函数 `0x100166b0` / `0x10016750..0x10016770`；Android `InitMirrorControl` 入口 `0x66fdd4` size 468（内部证据 `0x66fe08–0x66fe5c`）、`MatchMirrorVariable` 入口 `0x677864` size 2628（内部证据 `0x677930–0x6779b8`；`memchr` `0x677974`、`memcmp` `0x677988`） |
| `metadata.stereovisionControl.variableMatchList`：另一份列表，查询变量范围并建立各视图仿射映射 | DLL `0x1000af51..0x1000af97` → `0x10019d60`；`0x1001a2d0` 建系数；`0x1000c891..0x1000c901` 按 mirror 选择视图顺序，应用 coefficient*value+offset |

本次**没有证实顶层 `metadata.variableMatchList` 或“合法 pair 白名单”契约**。mirror 有效状态由元数据 mirror 与用户 mirror 状态组合（DLL `0x1000aad5..0x1000aae6`；Android `InitMirrorState` `0x6690b0` / `SetMirror` `0x67d484`）。真实条目、所有字符串/路径规范化、stereovision 完整系数与退化视图数边界仍未闭环。

普通 pair 身份/聚合、基础 getter 分离和正常范围公式足以设计独立实现与合成回归；**b 未进入基础缓冲的初始化保证、专用 eye/bust/selector 等覆盖顺序、queuing 全边界、异常浮点行为及真实模型输出未验证**。不以复杂为由任选 +v/-v。

## 5. E3：flags、Pass、Step 的更正

### E3-A：Lua 注册、公开 wrapper 与 relocation 短链

Android `artemis::CEmoteSurface::RegisterLua(lua_State*)` 位于 `0x5509b0`。下表将 Lua 字符串、注册结构和方法指针 relocation 对应起来，不只凭字符串猜行为：

| Lua 名 | RegisterLua 引用位置 | relocation VA → 公开 proxy VA |
|---|---|---|
| playTimeline | `0x551a88..0x551aa4` | `0x8d1cd8` → `CEmotePlayerProxy::PlayTimeline` `0x5527c8` |
| setVariableDiff | `0x55165c..0x551678` | `0x8d0998` → `SetVariableDiff` `0x5526d8` |
| getVariableDiff | `0x5516a8..0x5516c4` | `0x8d05e0` → `GetVariableDiff` `0x5526e8` |
| setQueuing | `0x550e28..0x550e44` | `0x8d3ae8` → `SetQueuing` `0x552500` |
| getQueuing | `0x550e6c..0x550e88` | `0x8d01c0` → `GetQueuing` `0x552514` |
| pass | `0x551cfc..0x551d18` | `0x8d4a90` → `Pass` `0x55285c` |
| step | `0x551d40..0x551d5c` | `0x8d0058` → `Step` `0x55286c` |
| progress | `0x551d84..0x551da0` | `0x8d0ed0` → `Progress` `0x55287c` |

| 方法短链 | 静态确认边界 |
|---|---|
| proxy PlayTimeline `0x5527c8` → player 虚表偏移 `0x1e0` → `PEmotePlayer::PlayTimeline` `0x65f660` → PLT `0x388c20` / GOT `0x8cb2c0` → `MEmotePlayer::PlayTimeline` `0x6680ac` | proxy 原样传 label/flags；P 层仅转换字符串表示，未将 bit1 翻译成 sequential |
| proxy Pass `0x55285c` → `PEmotePlayer::Pass` `0x65fd2c` → PLT `0x38a9d0` → `MEmotePlayer::Pass` `0x6757c8` | 未改成“完成全部过渡” |
| proxy Step `0x55286c` → `PEmotePlayer::Step` `0x65fd34` → PLT `0x387d70` → `MEmotePlayer::Step` `0x6751d8` | 未改成固定一帧 Progress |
| proxy Progress `0x55287c` → `PEmotePlayer::Progress` `0x65fd3c` → `MEmotePlayer::CalcLayerFrameInfo(float)` `0x6728cc` | 独立的 elapsed/帧计算入口 |

Android player 的地址点/槽布局不同：PlayTimeline 偏移 `0x1e0` 对应地址点索引 60，不能套 Windows slot 57；本次不重写 E1 表。

### E3-FLAGS：bit0 全清；bit1 私有差分

- `0x100172c6..0x100172ce` 检查 PlayTimeline 的 bit0；为零时构造**空字符串**（常量 `0x1008cb5c` 的原始字节为零），`0x100172fa` 调 `StopTimeline("")`。
- StopTimeline 的空串分支 `0x10017618..0x1001764f` 清空整个播放列表。故 **bit0=0 先停止全部 timeline**，不是旧文档写的 `StopTimeline(label)`；bit0=1 跳过本次全清。
- Android 同样在 `0x668280/0x6682d0` 检查 bit0；`0x6682d4..0x6682e4` 构造空字符串，经 PLT `0x380e70` 调 `MEmotePlayer::StopTimeline` `0x667d30`。结合上述 Lua 注册与原样转发链，**该 Android 样本的 Lua bit0=0 全清已有直接静态证据**，不再只是文档分层推断。

| bit1 数据流 | Android VA / Windows 对照 |
|---|---|
| 启动时 blend=1，清 fade-out-stop 和首次跨循环标记，保存 flags；bit1 命中时，为有关键帧且非 instant 的 timeline variable 创建/清零私有一维控制器 | `InitTimelineFlags` `0x67dea4`，关键路径 `0x67def8..0x67df80`；DLL 对照 `0x10018dd0` |
| bit1 不只初始化：Cue/Forward 时，非 instant variable 的命令进入私有差分控制器，而不是普通 SetVariable | `CueTimeline` `0x67dfc8`；`ForwardTimeline` `0x668470`，选择 `0x6684f0..0x668508`、提交 `0x668570..0x668600` / `0x6686d0` |
| 私有变量控制值乘 timeline blend，加回同 label 变量 | `AddTimelineDifference` `0x6776a4`，筛选 `0x6776ec..0x677724`，累加 `0x6777d0..0x6777e8` |

这里的单位是 **timeline variable 轨道**，不是渲染 layer；instant 标记来自 `instantVariableList` 的集合判定（`InitInstantVariable` `0x66ffa8`；`InitTimeline` `0x67db88..0x67dba8`）。**bit1=私有差分路径，不是整条播放队列，也没有证据等价于 owner 基础 motion/全部动画重新初始化。** 不应保留 `flags2=sequential` 的官方兼容表述。

### E3-PASS

Windows slot 66 → `0x100035e0` → `0x1000bb10` → `0x10019960`；Android `Pass` `0x6757c8` 除记录外只调用 `PassTimelineControl` `0x675968`，不执行全局 epSkip。

| 正常 timeline 状态 | Pass 转移 | Android 证据 |
|---|---|---|
| loopBegin>=0 | 跳过该 timeline | `0x6759d4..0x6759dc` |
| loopBegin<0，bit1=1，内部 bit2 已置位 | 跳过，避免重复 Pass | `0x6759e0..0x6759ec` |
| loopBegin<0，bit1=1，内部 bit2 未置位 | 提交 blend→0、duration=20、ease=0、fade-out-stop=true；置内部 bit2；保持播放项供淡出 | `0x6759b0`、`0x675a78..0x675aac` |
| loopBegin<0，bit1=0 | 按下述规则提交后续关键帧命令，然后移出播放集合 | `0x675b80..0x675c04`、`0x675c30` 起 |

关键帧规则：从每 variable 当前游标的**下一项**起，依次遍历后续记录，跳过 `type==0`；bit1=0 时处理各 variable，bit1=1 时只处理 instant variable。对有效记录调用 `SetVariable(label,content.value,record.time,content.easing)`（筛选 `0x675abc..0x675b28`，参数/调用 `0x675b40..0x675b50` / `0x675bf4..0x675c04`）。它不是简单采样最终姿态，也不等于将 position 推到 lastTime；多个命令的结果仍受 queuing 影响。Windows 后续关键帧处理对照 `0x10019a8c..0x10019b2b`。

**E3-CLOCK**：Android `CEmoteSurface::Display` `0x550878` 在 `0x5508b4` 调 `CTime::Now` `0x56cfe0`（clock_gettime 秒/纳秒换算成毫秒）；`0x5508c4..0x5508f4` 将 elapsed 毫秒差限制到 1000 后乘约 `0.06`，传给 Progress。因此该样本的 native 时间是 60fps 帧单位；Pass 的 **20 native frames 约等于 333.3ms，不是 20ms**。不把此结论外推为其他版本/Windows 宿主已认证的时钟契约。

### E3-STEP / SKIP：边界提交，不推进 base frame

元数据字段由 Android `InitTimeline` `0x67d8d0` 读取：loopBegin `0x67d920..0x67d944`、loopEnd `0x67d93c..0x67d964`、lastTime `0x67d95c..0x67d97c`。下表是正常有限值路径，畸形循环不作约定。

| 入口 / 条件 | 可确认的 timeline 转移 | Android VA |
|---|---|---|
| Skip，loopBegin<0 | ForwardTimeline(lastTime,inclusive=true)，移出播放列表 | `SkipTimelineControl` `0x674dc4`，`0x674e04..0x674e20` |
| Skip，loopBegin>=0 | 保留循环播放项，完成其 blend 控制器命令；不固定增加一帧 | `0x674ee0..0x674eec` |
| Step，loopBegin<0 或 fade-out-stop 非零 | ForwardTimeline(lastTime,inclusive=true)，移出播放列表 | `StepTimelineControl` `0x675580`，`0x6755d0..0x6755fc` |
| Step，其余循环项且尚未跨循环 | ForwardTimeline(loopEnd,false) → CueTimeline(loopBegin) → ForwardTimeline(loopBegin,true)；bit1 时按 loopEnd−调用前 current 推进私有差分控制器；标记已跨循环 | `0x6756bc..0x675780` |
| Step，已跨循环的保留项 | 不重复上述跨界；完成 blend 控制器命令 | `0x6756bc..0x675788` |

公开 `MEmotePlayer::Step` `0x6751d8` 与 `Skip` `0x674a1c` 在各自 timeline 操作之后，还完成多类控制器命令：transition/rotate/bust/pend/eye/eyebrow/mouth/selector/pair 等。对一维 `EPTransitionControl::epSkip`（`0x697604`），有排队命令则采用**最后排队目标**，否则有活动过渡则采用其目标，并清理活动/排队状态；不是每次只弹一个命令。

Step 的完整公开短链未调用 Progress、CalcLayerFrameInfo、MMotionPlayer 帧计算或 CTime，也未固定增加 1 帧；它可能跳 timeline position，但**不增加 base frame/基础 motion 时钟**。Progress 的独立路径进入 `_CalcLayerFrameInfo` `0x673390`，执行 elapsed 累积、timeline/pair/控制器更新和 MMotionPlayer 帧计算。纠正旧笔记“Step/Skip 后整体重算”的过强表述：目前确认的是控制器提交/完成，不能宣称立即完整重新求值场景。Windows 对照入口为 Skip `0x10019660`、Step `0x100197a0`，不凭相似调用链将不同平台完整状态机视为已动态等价。

### E3-QUEUE：独立 Lua API 与过渡命令队列

| 事实 | Android 证据 |
|---|---|
| setQueuing/getQueuing 均注册到 Lua，并直接转发 | 上述 RegisterLua relocation；proxy `0x552500` / `0x552514` |
| 独立 bool，初始化 false | `MEmotePlayer::SetQueuing` `0x6687e0`、`IsQueuing` `0x6687ec`；初始化 `0x662c9c` |
| bool 传入多个 EPTransitionControl::epSetCommand；影响 transform、变量/pair、blend 和私有 timeline variable 命令 | Cue `0x67e1cc`；Forward `0x6685fc` / `0x6686cc`；blend `0x67e88c`；命令入口 `0x6977bc` |
| 正 duration 且 queuing=true 追加命令；false 走替换/清理路径（含相同目标保留优化）；非正 duration 立即采用目标并清理 | `EPTransitionControl::epSetCommand` `0x6977bc..0x697b98` |

这是**过渡命令队列，不是 timeline 播放项 sequential 队列**；不得将 SetQueuing 机械实现成“等上一条 timeline 结束”。

### 可实现正常事实与未证边界

上述正常路径足以独立设计 flags0 全清、bit1 私有差分、控制器 queuing、Pass/Step/Skip 的合成断言；**不改变未知 getter 为猜测值**。异常浮点/整数溢出、畸形 loop、bit2 外部输入与内部 Pass 标记的全部约束、特殊控制器覆盖顺序、真实脚本在这些调用后何时 Progress、stereovision 及所有版本的动态视觉结果仍未证。实现 agent 正在独立对齐，本文不先称实现/回归/真实模型已通过。

## 6. E6/E7：结构过滤、纹理 callback 与容器证据分开

| 导出 | VA | 导出符号提供的边界 |
|---|---|---|
| `?EmoteCreate@@YAPAVIEmoteDevice@@ABUInitParam@1@@Z` | `0x100038d0` | 返回 IEmoteDevice 指针，接受 InitParam const 引用；host 完整协议未知 |
| `?EmoteCheckValidObject@@YA_NPBEK@Z` | `0x10003ed0` | const byte 指针、unsigned long 长度，bool 返回 |
| `?EmoteFilterTexture@@YAXPAEKP6AX0K@Z@Z` | `0x10003920` | 可写 byte 指针、unsigned long 长度、接受 byte 指针和长度的 callback，void 返回 |

**E6-FILTER（局部行为高）**：EmoteFilterTexture 构造结构过滤/解析对象，读取根 `source`，遍历 `texture`，检查 `type=RGBA8`，取 `pixel` 数据指针和长度，在 `0x10003c81` 调用户 callback；随后处理 `mipMap` 的 `pixel`，另一调用点在 `0x10003d56`。因此 callback 是**纹理像素缓冲过滤**，不是“回调整个解密 PSB 输出”。两入口存在固定 seed 初始化路径；本文不保存密钥材料。EmoteCheckValidObject 先分配并复制 caller 输入，再进入过滤/解析路径，不能将其说成直接修改输入的解密 API。

结构过滤的全部字节范围、流起点/续接、失败处理和整包输出契约未恢复；没有真实整包加密模型，不能将 E6 标为真实样本验收完成。

项目 `src/pack/psb.cpp` 支持 PSB v2 body 解密、宿主 seed、MDF/zlib 和 LZ4 frame 包装，这是**自有实现事实**。旧文档关于其他兼容宿主的加载规则属于不同来源，不能反推这个 DLL 的完整容器能力；本次没有取得该 DLL 支持 MDF/LZ4 的充分直接证据，也没有重新访问外部实现。

**E7-SPLIT**：DLL 含 `sourceIconRenameMap` 字符串（`0x1008d4d0`）。这不足以证明拆分文件如何合并、资源重编号、名称冲突优先级或多文件创建 ABI。指定目录无拆分样本；项目 Lua 创建入口目前拒绝 multi-file。物理合并与附加文件共同查找都只是待样本验证的方案，不是官方已证格式。

## 7. E13：record/replay 静态接口，动态未完成

**E13-RECORD**：内部 player `+0x1d8` 的模式值为 0/1/2；包装对象通过 `+0x14` 取得内部 player。`+0x1dc` 是命令列表相关结构，不能只称为连续文本缓冲；导出字符串对象在 `+0x1e4`。

| slot0 | 局部行为（正式方法名仍未知） |
|---:|---|
| 86 | 将模式设为 1 |
| 87、90 | 相同入口，将模式设为 0 |
| 88 | 返回模式是否为 1 |
| 89 | 将模式设为 2，设置列表游标 `+0x200`，将 `+0x204` 设为 1.0 |
| 91 | 返回模式是否为 2 |
| 92 | 清空命令列表和导出字符串，模式归零 |
| 93 | 经 `0x1001ad30` 将列表串成文本并返回内部字符串指针；每条追加 CRLF |
| 94 | 转发 `0x1001ad80`，接受文本并进入命令列表解析路径 |

记录路径还检查 `+0x1fc` 嵌套计数是否为 1；不能认为内部嵌套调用全都独立记录。格式化缓冲大小 0x400，并有 BufferOverflow 分支。CalcLayerFrameInfo 在模式 2 下先调 `0x1001ae50`，若仍在回放模式则使用 `+0x204` 的时间值。

完整转义/编码、错误文本、回放结束、字符串有效期、host ABI 和真实轨迹差分均未验证。**未确认可执行的 Wine/Windows/D3D9 参照环境；没有运行 DLL，不作 E13 动态验收。**

## 8. Lua API / 回归 / 不实现边界（历史基线，非当前验收）

这是**首次调查时、实现 agent 后续并行改动之前**的工作区基线，非最新实现能力表。E2/E3 已分派按本文件后续证据独立对齐，下表只保留“为什么旧断言不能作官方证据”的追溯，不断言改动后代码仍有相同缺口，也不抢先报告通过。Lua 注册见 `src/script/lua_engine.cpp` 的 `kEmoteMethods`，实现见 `src/render/emote_player.{h,cpp}`；实际进度以实现提交和独立验证为准。本次只核对静态事实，没有运行项目测试。

| DLL 已知面 / 未知面 | 项目 Lua/API 状态与差异 | 已有回归或明确边界 |
|---|---|---|
| SetCoord / SetScale（21/23） | Lua setCoord(x,y,z,angle)、setScale(scale,origin_x,origin_y)，不同于 native 记录参数解释；不直接绑定二进制 ABI | emote_regressions 的 angle/origin、即时 placement 保持断言；官方插件映射待证 |
| SetRot / SetColor / SetGrayscale（25/27/29） | 已注册 setRot/setColor/setGrayscale；Lua 帧时长转换与 native 时钟需分层；颜色布局不能由 `%d` 独立证明 | emote_regressions 的颜色/灰度/过渡断言；像素回归不能代替官方轨迹 |
| SetVariable（40） | 已注册 setVariable/getVariable；getter 正式 DLL 槽名未恢复，不按相邻槽猜配 | emote_regressions 的未知变量、ease 正负分支；官方最终混合待证 |
| SetVariableDiff / pair getter（42/43） | 旧基线为 +v/-v 双写且未注册 getter；§4 已证 second 聚合与 base getter 分离；实现 agent 正在对齐 | 旧 equal-and-opposite 断言是项目假设，不是官方证据；新合成回归及真实模型验收须单独记录 |
| PlayTimeline / StopTimeline（57/59）及 queuing | 旧基线未做 bit0=0 全清、bit1 为 sequential、空串 Stop 未全清；§5 的 Android wrapper 已反证“宿主层翻译成 sequential”解释 | 旧 queue 回归不是官方证据；后续须验证私有差分、独立命令队列，不将过时断言当兼容结论 |
| Blend / FadeIn / FadeOut（60/62/63） | 已注册相应 API，支持过渡及零 blend 移除；native 全 flags 域未穷尽验证 | emote_regressions 的 fade-in/out、direct blend、auto-stop 断言；完整官方边界仍待证 |
| Skip / Pass / Step（65/66/67） | 旧基线 Pass 完成全局过渡、Step 固定一帧；§5 已确认这些不是该 Android wrapper 的行为 | 旧 pass completes / 60 次 step 断言不是官方证据；新的边界提交和不加 base frame 回归由实现任务维护 |
| SetOuterForce / SetOuterRot（44/84） | 初次读取时未注册具体物理方法，落入 logging stub；交付检查已观察到其他 agent 新增同名注册，不能仅据注册声称实现物理 | 本次不实现/不新增代理；后续显式不支持代理与完整物理必须区分，需要真实模型及独立物理契约 |
| CalcLayerFrameInfo（68） | 不公开同名 Lua 方法，项目由内部场景求值/渲染承担；输出 ABI 未恢复 | 本次不暴露 native 层数组；不将相似用途称为结构兼容 |
| record/replay（86..94） | 正式槽名未知，未注册记录/回放 Lua API | 不实现二进制调用桥；E13 动态工具待环境和 host ABI |
| 引用计数/析构、device 创建/配置 | Lua userdata 有自有生命周期，不直接调用官方 DLL；device 配置正式名和签名未知 | 本次不新增二进制 ABI 层；未知配置不按字段偏移猜功能 |
| 其他未知 player 槽 | Lua 还暴露枚举/getters/show/hide/contains/progress 等；没有逐槽官方证据，不把这些名字填入未知槽 | 仅保留自有 API/合成回归；不宣称覆盖全部 native 方法 |
| EmoteFilterTexture / CheckValidObject；E7 | 无同名 DLL bridge；项目 DecodePsb 支持的容器不等于官方导出等价；多文件显式拒绝 | psb_encrypted_regressions 为合成解密/容器断言；真实整包和拆分样本缺失 |

**状态：**E1 原始槽地址清单已完整归档；正式签名/大部分名称及宿主映射仍不完整。E2/E3/E6/E7/E13 未因本次静态研究获得真实模型或 Windows 动态验收。主/detail 计划状态由其他任务维护，本次不修改。

Android 的已确认 Lua 方法只限定固定样本，不将其额外符号填入 Windows CSV 未知槽；本次不新增/猜测 timeline、record/replay 或变量 getter 的返回规则。

## 9. 只读复现与 CSV 校验

在仓库根目录运行；不复制 DLL、不加载执行 DLL，也不生成反编译输出。`pd` 是本地静态查看，不应将实现指令输出提交仓库。

```bash
shasum -a 256 /Users/weiss/Desktop/emote-test/emotedriver.dll
stat -f '%z bytes' /Users/weiss/Desktop/emote-test/emotedriver.dll
rabin2 -I -E /Users/weiss/Desktop/emote-test/emotedriver.dll
rabin2 -z /Users/weiss/Desktop/emote-test/emotedriver.dll | grep -E 'Set|Fade|Play|Skip|Step|Pass|Calc|variableMatchList|sourceIconRenameMap'
r2 -q -e scr.color=0 -e bin.relocs.apply=true -c 'pxw 380 @ 0x1008b900; pxw 380 @ 0x1008ba88; pxw 100 @ 0x1008b898; pxw 100 @ 0x1008bc08; q' /Users/weiss/Desktop/emote-test/emotedriver.dll
r2 -q -e scr.color=0 -e bin.relocs.apply=true -c 'pd 55 @ 0x1001eac0; pd 110 @ 0x100172c6; pd 65 @ 0x10017618; pd 210 @ 0x10019960; q' /Users/weiss/Desktop/emote-test/emotedriver.dll
r2 -q -e scr.color=0 -e bin.relocs.apply=true -c 'pd 20 @ 0x10003c6f; pd 20 @ 0x10003d38; pd 80 @ 0x10003750; q' /Users/weiss/Desktop/emote-test/emotedriver.dll
```

以下标准库脚本从 PE section 表计算 VA→文件偏移，重新读取全部 240 个源表 DWORD（CSV 120 对），检查行数、顺序、编号、偏移、哈希和四张表的边界；不依赖 r2 的自动函数识别。CSV 的行为分类/名称还需按证据地址人工复核，不能由这个字节校验器证明。

```bash
python3 - <<'PY'
import csv, hashlib, struct
from collections import Counter
from pathlib import Path
b = Path('/Users/weiss/Desktop/emote-test/emotedriver.dll').read_bytes()
assert len(b) == 1091072
assert hashlib.sha256(b).hexdigest() == '40d2e510106fc8a79e0fd07e5c0d4839d75503089d6fc9de0f0904f0ab73bd4d'
pe = struct.unpack_from('<I', b, 0x3c)[0]
assert b[pe:pe+4] == b'PE\0\0'
opt = pe + 24
assert struct.unpack_from('<H', b, opt)[0] == 0x10b
base = struct.unpack_from('<I', b, opt+28)[0]
assert base == 0x10000000
n = struct.unpack_from('<H', b, pe+6)[0]
size = struct.unpack_from('<H', b, pe+20)[0]
sections = []
for i in range(n):
    o = opt + size + i*40
    _, va, raw_size, raw_offset = struct.unpack_from('<IIII', b, o+8)
    sections.append((va, raw_size, raw_offset))
def offset(va):
    rva = va - base
    for start, length, raw in sections:
        if start <= rva and rva + 4 <= start + length:
            return raw + rva - start
    raise ValueError(hex(va))
def word(va):
    return struct.unpack_from('<I', b, offset(va))[0]
tables = {'player': (0x1008b900, 0x1008ba88, 95),
          'device': (0x1008b898, 0x1008bc08, 25)}
rows = list(csv.DictReader(Path('docs/emote-driver-slots.csv').open(newline='')))
assert len(rows) == 120
assert Counter(r['table'] for r in rows) == {'player': 95, 'device': 25}
assert [(r['table'], int(r['slot0'])) for r in rows] == [
    (name, i) for name, (_, _, count) in tables.items() for i in range(count)]
for r in rows:
    iv, pv, count = tables[r['table']]
    i = int(r['slot0'])
    assert r['non_dtor0'] == ('NA' if i == 0 else str(i-1))
    assert int(r['byte_offset'], 16) == i*4
    assert int(r['interface_target_va'], 16) == word(iv+i*4)
    assert int(r['implementation_target_va'], 16) == word(pv+i*4)
    first = 0x10001330 if r['table'] == 'player' else 0x10001260
    assert word(iv+i*4) == (first if i == 0 else 0x10067fa3)
    assert r['full_signature'] == 'unknown'
    assert r['mapping_status'] == 'static_index_only'
for va, expected in [(0x1008ba7c, 0x100952a4), (0x1008bc04, 0x10094fdc),
                     (0x1008b8fc, 0x10095214), (0x1008bc6c, 0x100952d0)]:
    assert word(va) == expected
assert sum(r['method_name'] != 'unknown' for r in rows) == 18
print('PASS: 120 rows = player 95 + device 25; all paired pointers match the pinned DLL')
PY
```

### Android 身份、动态符号、Lua relocation 与 wrapper 的只读校验

以下为文件格式/接口证据检查脚本，不包含第三方方法实现。只用标准库从嵌套 ZIP 读入内存，检查固定身份、ELF dynsym/rela、Lua 字符串及已记录注册方法指针；不保存库/指令输出。`show(va,size)` 的可选调用只在本机终端局部静态查看；**rasm2 的起始地址参数是 `-s`，`-o` 是写出文件，不得用于地址参数**。默认不开启 show，故脚本只打印身份/名称/地址事实，不输出第三方指令。

```bash
python3 - <<'PY'
import hashlib, io, struct, subprocess, zipfile
from pathlib import Path
p = Path('/Users/weiss/Desktop/TyranorNext稳定版/TyranorNext-1.42.apk')
apk = p.read_bytes()
assert len(apk) == 114704670
assert hashlib.sha256(apk).hexdigest() == '035349b12b8af648a1b8cd841ecd3b9b50f6c8ac880b33eb5fa8c538c7ea3e6d'
with zipfile.ZipFile(io.BytesIO(apk)) as z:
    plugin = z.read('assets/nativeplugins/artemis.zip')
assert len(plugin) == 17126424
assert hashlib.sha256(plugin).hexdigest() == '02bd818b3550551c3a309355f14da9f4108a3b35eeb7c37e2fcbf382763e897d'
with zipfile.ZipFile(io.BytesIO(plugin)) as z:
    b = z.read('arm64-v8a/libartemis.so')
assert len(b) == 9292144
assert hashlib.sha256(b).hexdigest() == 'cb2b58d8080b29529b27a8453f602646ab5f13ae5d4acc59b673c27c27e2d9cc'
assert b[:6] == b'\x7fELF\x02\x01'
assert struct.unpack_from('<H', b, 18)[0] == 183  # AArch64
shoff = struct.unpack_from('<Q', b, 40)[0]
entsize, count, _ = struct.unpack_from('<HHH', b, 58)
sections = [struct.unpack_from('<IIQQQQIIQQ', b, shoff+i*entsize)
            for i in range(count)]
def read_va(va, size):
    for s in sections:
        if s[1] != 8 and s[3] <= va and va+size <= s[3]+s[5]:
            off = s[4]+va-s[3]
            return b[off:off+size]
    raise ValueError(hex(va))
symbols, by_name, reloc = [], {}, {}
for s in sections:
    if s[1] == 11:  # .dynsym; relocation symbol indices refer here
        strings = sections[s[6]]
        names = b[strings[4]:strings[4]+strings[5]]
        for off in range(s[4], s[4]+s[5], s[9]):
            n, info, other, index, va, size = struct.unpack_from('<IBBHQQ', b, off)
            name = names[n:].split(b'\0', 1)[0].decode()
            symbols.append((name, va, size, index))
            by_name[name] = (va, size, index)
    elif s[1] == 4:  # SHT_RELA
        for off in range(s[4], s[4]+s[5], s[9]):
            place, info, addend = struct.unpack_from('<QQq', b, off)
            reloc[place] = (info >> 32, info & 0xffffffff, addend)
bindings = [
    ('playTimeline', 0x7cf9b5, 0x8d1cd8, '_ZN17CEmotePlayerProxy12PlayTimelineEPKcj', 0x5527c8),
    ('setVariableDiff', 0x7cf89e, 0x8d0998, '_ZN17CEmotePlayerProxy15SetVariableDiffEPKcS1_fff', 0x5526d8),
    ('getVariableDiff', 0x7cf8ae, 0x8d05e0, '_ZNK17CEmotePlayerProxy15GetVariableDiffEPKcS1_', 0x5526e8),
    ('setQueuing', 0x7cf71f, 0x8d3ae8, '_ZN17CEmotePlayerProxy10SetQueuingEb', 0x552500),
    ('getQueuing', 0x7cf72a, 0x8d01c0, '_ZNK17CEmotePlayerProxy10GetQueuingEv', 0x552514),
    ('pass', 0x7cfa38, 0x8d4a90, '_ZN17CEmotePlayerProxy4PassEv', 0x55285c),
    ('step', 0x769345, 0x8d0058, '_ZN17CEmotePlayerProxy4StepEv', 0x55286c),
    ('progress', 0x7cfa3d, 0x8d0ed0, '_ZN17CEmotePlayerProxy8ProgressEf', 0x55287c),
]
assert by_name['_ZN7artemis13CEmoteSurface11RegisterLuaEP9lua_State'][:2] == (0x5509b0, 6796)
for label, string_va, place, name, expected in bindings:
    assert read_va(string_va, len(label)+1) == label.encode()+b'\0'
    index, kind, addend = reloc[place]
    assert kind == 1025 and symbols[index][0] == name  # R_AARCH64_GLOB_DAT
    assert symbols[index][1]+addend == expected
    print('binding target:', label, hex(place), name, hex(expected))
def plt_target(va):
    adrp, ldr = struct.unpack_from('<II', read_va(va, 8))
    assert adrp & 0x9f000000 == 0x90000000
    assert ldr & 0xffc00000 == 0xf9400000
    immediate = ((adrp >> 5) & 0x7ffff)*4 + ((adrp >> 29) & 3)
    if immediate & (1 << 20):
        immediate -= 1 << 21
    got = (va & ~4095)+(immediate << 12)+((ldr >> 10) & 4095)*8
    index, kind, addend = reloc[got]
    assert kind == 1026  # R_AARCH64_JUMP_SLOT
    return symbols[index][0], symbols[index][1]+addend
assert plt_target(0x388c20)[1] == 0x6680ac  # MEmotePlayer::PlayTimeline
assert plt_target(0x380e70)[1] == 0x667d30  # MEmotePlayer::StopTimeline
assert plt_target(0x38a9d0)[1] == 0x6757c8  # MEmotePlayer::Pass
assert plt_target(0x387d70)[1] == 0x6751d8  # MEmotePlayer::Step
assert plt_target(0x38ade0)[1] == 0x6728cc  # CalcLayerFrameInfo
def show(va, size):
    subprocess.run(['rasm2', '-a', 'arm', '-b', '64', '-s', hex(va),
                    '-D', read_va(va, size).hex()], check=True)
# Optional LOCAL inspection only; leave disabled for facts-only validation:
# show(0x550e18, 0x78)  # RegisterLua setQueuing/getQueuing references
# show(0x551a7c, 0x30)  # RegisterLua playTimeline reference
# show(0x5527c8, 16)    # proxy direct virtual forwarding
# show(0x6682d0, 0x28)  # flags0 empty StopTimeline
# show(0x67dea4, 292)   # private difference initialization
# show(0x675968, 1004)  # PassTimelineControl
# show(0x675580, 584)   # StepTimelineControl
# show(0x674dc4, 344)   # SkipTimelineControl
# show(0x6977bc, 1036)  # transition command queue branches
# show(0x5508b4, 0x44)  # elapsed milliseconds * 0.06 to Progress
print('PASS: pinned nested identities, public binding targets and PLT/GOT targets')
PY
```

此脚本的 binding 校验验证字符串和 relocation 目标；RegisterLua 中二者的注册结构关联、条件分支和状态迁移仍需按表列 VA 局部核对，不能把脚本 PASS 当作完整行为证明。静态检查和合成断言均不代替真实模型/宿主动态差分验收。
