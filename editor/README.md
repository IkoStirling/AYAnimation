# 独立骨骼与动画作者核心

2026-09-29。链接 `AYAnimationEditorCore`；命名空间 `ayt::anim::editor`。
核心不依赖 AYEditor/AYUI。资源读取、数学、离线烘焙和作者变换留在这里，
页面仅适配 opaque ID、命令历史、选择、播放推进和保存。

## 入口与所有权

| 入口头 | 用途 |
| --- | --- |
| `SkeletonEditorCore.h` | 源骨架检查、RigProfile、映射/参考姿势/骨轴、预览及 dry-run |
| `SkeletonBakeJob.h` | AYTask owned 异步执行、状态轮询、取消及 drain |
| `AnimationPreviewSession.h` | Clip-only transport、可选骨架/网格绑定及模型/骨架预览 |
| `AnimationAuthoring.h` | detached 作者数据、跨轨/Notify 原子移动和删除 |
| `AnimationClipboard.h` | owned 内存剪贴板，兼容已有轨道的粘贴 |
| `AnimationTimeTransform.h` | 秒制锚点缩放、反向与 Hermite 切线修正 |

`AuthoredAnimation` 是短生命周期编辑副本，不是平行运行时资源系统。
变换返回新的 `Animation` 和与输入选择位置对应的新 `AnimationKeyReference`；
失败不修改 source。Notify 的 track 字段不参与身份，key 是原 Notify 下标。
宿主验证成功后一次提交，不逐键写入；重新排序时必须应用返回的新选择。

```cpp
#include <AYAnimationEditor/AnimationAuthoring.h>
#include <AYAnimationEditor/AnimationClipboard.h>
#include <AYAnimationEditor/AnimationTimeTransform.h>

using namespace ayt::anim::editor;

// source 为宿主持有且本次调用内不并发修改的 IAnimation。
auto moveSelected(const ayt::resource::IAnimation& source,
                  const std::vector<AnimationKeyReference>& selection) {
    return translateAnimationKeys(source, selection, 0.1); // 秒
}
```

## 作者规则与编辑器接入

- 公共时间为秒；资源 times 保持 ticks；切线是 value/second。移动整组夹取到
  Clip 范围，保持间隔；碰撞、非法身份、非有限数据或相关轨道时间无序整体拒绝。
- Clipboard 保存骨名/属性/type/blend/interpolation、相对秒、值/切线和 Notify。
  粘贴要求每个签名匹配唯一已有轨道，不自动建轨、扩展 duration、覆盖或重定向。
  跨 TPS 保持秒数/斜率。AYEditor 另检查 skeleton binding；页面关闭不使 payload 失效。
- `retimeAnimationKeys` 使用 `t'=anchor+(t-anchor)*scale`；斜率除以有符号 scale，
  负比例交换入出切线。现有 Step 是 left-hold，无法精确表达反向 right-hold，故拒绝倒放。
- Quaternion 插入/曲线预览复用正式 KeySampler，禁止四条独立标量插值代替 rotation。
  Float 新增键使用宿主显式值，Vector/Quaternion 新增键使用已有轨道的正式采样值。
- AYEditor 的 DopeSheet 支持跨行框选/组移动；CurveCanvas 分量拖动仅编辑当前轨道。
  Copy/Cut/Paste at Playhead/Duplicate After、锚点缩放/倒放、菜单和 Ctrl+C/X/V/D
  走文档命令；文本焦点不抢剪贴板快捷键。baked/legacy 只读，仍可预览/复制。
- 文档仅 weak 绑定共享选择；Undo/Redo 恢复内容与选择。一个 drag 的连续历史
  合并为开始/最终状态，不保留所有中间序列化快照。各独立手势不互相合并。

## 预览与性能契约

没有骨架仍可 seek/play/stop 和编辑时间轴；几何姿势预览仍需有效骨架/网格。
热编辑和重新绑定骨架保留时间及播放状态。`revision` 仅标记内容/绑定变化；
playhead、loop、rate 不重建作者快照。宿主独立刷新 transport，不依据内容 revision
检测 loop/rate。旧不可变快照和采样闭包拥有数据，可在 Undo/Reload 后继续读取。

DopeSheet 按不可变快照身份缓存行索引；大选择使用哈希去重/重映射。
回归使用 5 万键批量、5 万项选择、200 行与 3.2 万键 Clip，检查对象身份和重建次数；
不把特定机器耗时阈值作为通过条件。尚无专用垂直滚动、LOD 或超大资产性能保证。

## 烘焙安全边界

dry-run 捕获源/目标/profile/引用闭包的 size:mtime 指纹，执行前和发布前复核；
源输入不得成为输出。staging/backup 使用独占保留目录；同一 canonical 输出根和
receipt 根在本进程内串行发布。混合创建/替换失败恢复旧文件；恢复失败保留备份并报告路径。
Windows 瞬时争用复用 AYIO 有限重试（250 ms 调度预算、最多 26 次），持续拒绝仍失败。

`cancel()` 只取消 Building；Committing 不可取消。Cancelled 不等于 worker 已回收，
关闭页面、开始冲突写入或停止注入 scheduler 前须 `drain()`；析构也显式 drain。
worker 不借用页面或 job 的 Impl。这里只提供进程内排他和运行期回滚，
不是跨进程锁、断电事务日志或密码学内容指纹。

引用闭包、Additive/未知 TRS 拒绝、目标 mesh 重绑定等边界见
[资源管线设计](../../../AYDocs/SKELETAL-ANIMATION-RESOURCE-PIPELINE.md)。

## 测试分层

45 个作者核心用例：fast 13（作者操作/剪贴板/时间变换），integration 31
（预览、映射、引用闭包、后台生命周期及发布回滚），stress 1（5 万键）。
完整层是三个互斥分区的并集；直接运行 executable 默认仍覆盖所有注册用例。

```powershell
ctest --test-dir out/build/windows-debug -L '^animation-editor-fast$' --output-on-failure
ctest --test-dir out/build/windows-debug -L '^animation-editor-full$' --output-on-failure
& scripts/tests/run-module-tests.ps1 -Module AYAnimationEditorCore -Tier full -SkipBuild -Audit
```

模块入口/统计见[统一测试契约](../../../AYDocs/testing.md)；连续阶段证据见
[实施记录](../../../AYDocs/animation-authoring-hardening.md)。
