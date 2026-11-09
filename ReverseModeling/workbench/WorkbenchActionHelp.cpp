/*
 * 文件描述：集中维护 Ribbon、菜单和视图工具条的用途、操作方法及使用条件。
 * 将 QAction 的名称、快捷键、可用状态和实时提示组合为自动换行的悬停帮助；
 * 不连接业务执行，不改变动作的使能条件或命令录制。
 */
#include <initializer_list>

#include <QAction>
#include <QKeySequence>
#include <QVariant>

#include "workbench/WorkbenchActionHelp.h"
#include "workbench/WorkbenchActions.h"

namespace reverse::workbench
{
namespace
{
QString escaped(const QString &text)
{
    return text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
}

void refreshHelp(QAction *action)
{
    auto title = action->text();
    if (!action->shortcut().isEmpty())
    {
        title += QStringLiteral("（%1）").arg(action->shortcut().toString(QKeySequence::NativeText));
    }
    auto help = QStringLiteral("<qt><table width=\"360\" cellspacing=\"0\"><tr><td><b>%1</b>")
                    .arg(escaped(title));
    const auto append = [&help](const QString &label, const QString &value)
    {
        if (!value.isEmpty())
        {
            help += QStringLiteral("<p style=\"margin-top:6px; margin-bottom:0\"><b>%1：</b>%2</p>")
                        .arg(label, escaped(value));
        }
    };
    append(QStringLiteral("作用"), action->property("help.purpose").toString());
    append(QStringLiteral("操作"), action->property("help.operation").toString());
    append(QStringLiteral("条件"), action->property("help.requirement").toString());
    append(QStringLiteral("当前"), action->property("help.context").toString());
    if (!action->isEnabled())
    {
        help += QStringLiteral("<p style=\"margin-top:6px; margin-bottom:0; color:#f0c681\">"
                               "当前不可用。请查看上述条件；任务执行或其他工具编辑期间也会暂时禁用。</p>");
    }
    help += QStringLiteral("</td></tr></table></qt>");
    // setToolTip 本身会发出 changed；比较内容，避免递归及轮询时的重复刷新。
    if (action->toolTip() != help)
    {
        action->setToolTip(help);
    }
}

struct HelpSpec
{
    ActionId id;
    const char *purpose;
    const char *operation;
    const char *requirement;
};
} // namespace

void setActionHelp(QAction *action, const QString &purpose, const QString &operation, const QString &requirement)
{
    const bool initialized = action->property("help.purpose").isValid();
    action->setProperty("help.purpose", purpose);
    action->setProperty("help.operation", operation);
    action->setProperty("help.requirement", requirement);
    if (!initialized)
    {
        QObject::connect(action, &QAction::changed, action, [action] { refreshHelp(action); });
    }
    refreshHelp(action);
}

void setActionHelpContext(QAction *action, const QString &context)
{
    if (action->property("help.context").toString() != context)
    {
        action->setProperty("help.context", context);
        refreshHelp(action);
    }
}

void initializeActionHelp(WorkbenchActions &actions)
{
    using enum ActionId;
    const HelpSpec specs[] = {
        {Open, "导入点云或三维模型。PCD 默认按毫米读取。",
         "点击选择 PCD、STEP/STP 或 CWRM 文件；下拉箭头可选择最近打开文件。",
         "请等待当前任务完成。导入新文件会替换当前工程内容。"},
        {ExportStep, "将当前模型导出为 STEP，包含已应用的几何修改。",
         "点击后选择保存位置和文件名。", "需要已有模型；修改重建参数后须先重新建模，编辑预览须先应用或退出。"},
        {ExportModel, "将当前模型几何导出为 CWRM v2。", "点击选择新文件名；不包含原始点云、重建参数和编辑历史。", "需要已有模型；编辑预览须先应用或退出。"},
        {ExportModelCloud, "将当前模型表面采样为 PCD，非原始扫描数据。", "选择采样间距和保存位置；间距越小点越密。", "需要已有模型，最多五百万点；已有扫描点请用导出点云。"},
        {ExportAndExit, "导出当前模型和任务结果，然后退出程序。",
         "先通过启动参数 --output_dir 或命令 project.set_output_dir 设置目录，再点击此按钮。",
         "需要最终模型及有效输出目录；成功后退出，失败时保留窗口并显示原因。"},
        {ExportCloud, "将当前完整点云导出为 PCD，包含已确认的截取和删除结果。",
         "点击后选择保存位置。", "需要已有点云；仅导入 STEP/CWRM 模型时不能导出 PCD。"},
        {Undo, "退回上一步工程编辑，例如截取点云、删除部件或修改板厚；最多保留 10 步。",
         "点击一次退回一步；想恢复刚撤销的操作，请点击“重做”。",
         "需要有可撤销的编辑。撤销也会录制为命令，不删除已录制的内容。"},
        {Redo, "恢复刚被撤销的工程编辑。",
         "先执行撤销，再点击此按钮恢复一步；连续点击可继续恢复。",
         "需要有可重做的记录；撤销后进行新的编辑会清空重做记录。重做也会被录制。"},
        {Reset, "恢复最近一次导入时的点云或模型，取消导入后的工程编辑。",
         "点击立即重置；若需恢复重置前状态，可点击“撤销”。",
         "需要当前工程存在可重置的修改。重置也会录制，不停止录制。"},
        {LocalReconstruct, "重建选中的部件，或将其当前占用范围转换为盒状障碍物，其他部件保持不变。",
         "选择体或模型树部件；包围盒不需要点云关联或轮廓参数。选择规则并预览，满意后应用。Shift 可多选。",
         "平板重建需要原始点云；包围盒依据当前模型，结果不参与工件镜像。支持撤销及命令录制。"},
        {CropCloud, "只保留选中的点云，移除其余点；不改写原始文件。",
         "显示点云，切换“可选：点”，左键拖动框选后点击；可用加选/减选组合选区。",
         "至少选中一个点。框选贯穿深度，包括遮挡点，作用于完整点云；支持撤销。"},
        {DeleteCloud, "删除选中的点云，保留其余点；不改写原始文件。",
         "显示点云，切换“可选：点”，框选要删除的区域后点击。",
         "至少选中一个点。框选贯穿深度，包括遮挡点；支持撤销。"},
        {Parameters, "调整点云重建的镜像、腹板端部收齐及可见孔过滤。",
         "点击打开左侧重建参数面板，调整识别选项后开始重建。",
         "需导入点云才能编辑参数；轮廓容差自动估计，孔选项只控制腹板内孔。"},
        {Reconstruct, "依据当前点云的板件识别结果生成模型。",
         "导入点云，检查重建参数后点击；完成后可选择部件、修改或导出 STEP。",
         "需要有效点云和板件识别结果；重建会替换当前模型。"},
        {StopReconstruct, "请求停止正在执行的重建。",
         "重建过程中点击，等待任务安全结束。", "仅在重建运行且尚未请求取消时可用。"},
        {PlateThickness, "修改等厚板件的厚度，保持中面、外轮廓和贯穿孔。",
         "选择一个或多个板件，输入毫米厚度，回车或点击“预览”；确认效果后“应用”。也可双击板厚标签。",
         "需要可见模型中的等厚平面板件；不会自动移动相邻板件。"},
        {ExtendContour, "在板面内向外扩展外轮廓，保持板厚和封闭内孔。",
         "选择板件，输入外扩距离，预览后点击“应用”。", "需要可见模型中的等厚平面板件。"},
        {ChannelSteel, "将矩形实体替换为等壁厚槽钢。", "选一个竖直开口侧面，设置壁厚及自动/自定义高度，预览后应用。", "水平放置的矩形实体；H > 2D，截面宽度 > D。"},
        {AngleSteel, "将矩形实体替换为带上翼板的角钢。", "选一个竖直开口侧面，设置壁厚及自动/自定义高度，预览后应用。", "水平放置的矩形实体；H > D，截面宽度 > D。"},
        {OuterPlate, "在所选板面外侧生成一块贴板，保留源板。",
         "选择板件的平面，填写外扩 D 和板厚 T；可勾选反向，预览后应用。",
         "需要可用的等厚平面板件及板面；新增板不继承源板封闭内孔。"},
        {BottomPlate, "在选定板件下方生成一块水平底板，沿世界坐标 Z 较低的一侧加厚。",
         "选择“水平板”或“两个竖直板”方式，选齐板件，填写外扩 D、板厚 T，预览后应用。",
         "按世界 Z 方向判定水平/竖直，允许 ±5° 偏差；需满足对应方式的板件数量和几何条件。"},
        {CrossPlate, "保持连续板 A，按其板厚打断交叉板 B，可选修改高度差。",
         "依次选择 A、B，设置高度差，预览后应用；支持一次撤销。",
         "需要两块不同的交叉实体板；关闭高度差时保留原有高度。"},
        {EndCapPlate, "独立调整封板两侧边缘的错边或凸边。",
         "选择封板、边缘和距离，预览后应用当前边缘。",
         "选择梁端肋板，自动识别相连腹板与两侧翼板，无需放平工件；自动错边距离为当前翼板厚度的 75%。"},
        {MirrorComplete, "以底板/腹板中面为镜像面，补全所选板件另一侧，保留原板。",
         "选择要补全的板件，检查镜像面，预览后点击“应用”。",
         "需要可见模型和可识别的底板/腹板镜像面；不改变原始点云。"},
        {MirrorCompleteAll, "补齐选中工件的全部相连板件，不包含其他独立工件或障碍物。",
         "先选择工件的任意一个部件；自动选中相连整组并补全，Ctrl+Z 整体撤销。",
         "使用该组内的重建腹板；没有已识别腹板时，从本组选择镜像参考底板。"},
        {WeldHole, "在板件底部角点开过焊缺口，为焊缝留出空间。",
         "选择板件及底边，选圆形/三角形、尺寸和角点 1/2；绿色体显示孔位，预览后应用。",
         "需要等厚实体板件及可识别底边；只在“应用”后修改模型。"},
        {BottomHole, "沿板件底边开圆形或矩形缺口。",
         "选择板件和底边，指定端点/中点参考、偏移和尺寸；查看绿色孔位，预览后应用。",
         "需要等厚实体板件及可识别底边；只在“应用”后修改模型。"},
        {Fillet, "将板件平面轮廓的角倒圆或切成倒角，保持板厚不变。",
         "选择边，Shift 点击可跨部件多选；选择类型并输入 R/D，回车或预览，满意后应用。",
         "需要等厚板件；尺寸受板长、板高及相邻轮廓空间限制。"},
        {Move, "沿 X/Y/Z 移动选中部件，改变实际模型位置。",
         "切换“可选：体”并选择部件；输入毫米位移或拖动轴柄，检查预览后确定。",
         "至少选中一个可见部件；先退出测量、基准面拾取或其他编辑。"},
        {Rotate, "绕指定轴旋转选中部件，改变实际模型方向。",
         "在体模式选择部件，指定 X/Y/Z 轴、旋转中心及正负角度，检查预览后确定。",
         "至少选中一个可见部件；先退出测量、基准面拾取或其他编辑。"},
        {DeleteModel, "从当前模型删除选中部件，STEP 导出同步更新；点云及源文件保持不变。",
         "切换“可选：体”，选择一个或多个部件后点击；误删可撤销。",
         "至少选中一个可见部件；点/线/面模式及其他编辑期间不可用。"},
        {Measure, "测量两点直线距离和 X/Y/Z 分量，单位 mm。",
         "点击进入测量，再依次点击两个点；靠近模型顶点会吸附。第三次点击开始新测量；Esc、右键菜单或再次点击按钮退出。",
         "需要可见点云或模型；先退出其他几何编辑。"},
        {BasePlane, "指定观察和点云剖视的参考平面；不移动模型，也不生成底板或修改板厚。",
         "点击后选“3 点”（依次选不共线三点）或“特征面”（选模型平面），检查预览后确定；再正视或设置剖视。",
         "需要可见点云或模型；特征面方式需要模型。先退出添加孔等其他编辑操作。"},
        {Deviation, "统计当前点云与重建板件表面的距离偏差。",
         "完成点云重建后点击，在命令窗口查看结果。",
         "需要点云及其最新重建模型；导入模型或经过开孔、改厚等不支持的几何编辑后不可用。"},
        {Select, "用当前点/线/面/体类型选择对象。",
         "左键点击选择；Shift/Ctrl 点击追加或取消，点击空白或 Esc 清空；点模式可拖框选点云。",
         "需要可见点云或模型；点云框选需“可选：点”。"},
        {AddSelection, "保留已有选择，再追加对象。",
         "点击此按钮，再点击模型对象；点模式拖动矩形可追加点云。", "需要可见点云或模型。"},
        {RemoveSelection, "从选择集合移除对象，不删除实际点云或模型。",
         "点击此按钮，再点击已选模型对象；点模式拖框移除选区内的点云选择。", "需要可见点云或模型。"},
        {SelectPoint, "拾取模型顶点或框选点云。",
         "单击模型顶点；Shift 点击追加/取消。拖动框选点云，拖边/角缩放，拖框内移动；松开更新选区。", "需要可见模型或点云。"},
        {SelectEdge, "拾取模型拓扑边，不选择三角网格细分线。",
         "点击边，Shift 点击追加/取消；可用于轮廓圆角和倒角。", "需要可见模型。"},
        {SelectFace, "拾取完整几何面。",
         "点击面，Shift 点击追加/取消；可用于特征基准面和外扩贴板选面。", "需要可见模型。"},
        {SelectSolid, "选择完整模型部件。",
         "点击部件，Shift/Ctrl 点击追加/取消；模型树也可多选。用于移动、旋转、删除及板件操作。",
         "需要可见模型。"},
        {ShowCloud, "显示或隐藏点云，保留原始数据。",
         "点击图标/文字切换显隐，点击右侧箭头选择高度渐变或纯色。", "导入点云后可看到效果。"},
        {CloudHeight, "根据点云高度着色，便于辨认结构。",
         "点击切换为高度渐变；需要显示点云。", "只改变显示，不影响重建和导出。"},
        {CloudSolid, "以统一颜色显示点云。",
         "点击切换为纯色；需要显示点云。", "只改变显示，不影响重建和导出。"},
        {BuildByPoints, "选择点云点或模型特征点构造轮廓并拉伸。",
         "选择添加/切除、参考方向和形状，依次选点；下一步调整尺寸，预览后应用。", "需要点云或模型；切除需已有实体。"},
        {ObstacleByPoints, "框选点云生成参考面上的盒状障碍物。",
         "先点击模型平面，再点击第 3 步进入点云框选；按住左键拖框，检查预览后应用。参考面决定剖视方向和障碍物贴合面。", "需要点云和模型平面；曲面不能作为参考面，支持加选/减选。"},
        {ShowModel, "显示或隐藏模型，保留实际几何。",
         "点击图标/文字切换显隐，右侧箭头打开显示方式菜单。", "需已有模型；部分编辑期间锁定显示。"},
        {ShowThickness, "显示可见侧板件厚度标签，单位 mm，保留一位小数。",
         "点击开关标签；双击数值标签可输入新板厚，回车应用，Esc 取消。",
         "需要模型及板厚信息；正在测量或预览其他编辑时不能修改标签。"},
        {ModelSolid, "以实体表面显示模型。", "点击选择实体显示。", "只改变显示方式。"},
        {ModelWire, "以真实拓扑边显示模型线框。", "点击选择线框显示，可透过表面查看背面轮廓。", "只改变显示方式，不影响几何和导出。"},
        {Section, "按所选底板面或基准面隐藏一侧点云。",
         "主按钮开关剖视；箭头打开平面和偏移设置，选择后立即应用。",
         "需要点云及可用平面；只过滤显示，重建和导出使用完整数据。"},
        {BottomView, "未重建时使用上视图，重建后使用底板面；剖视开启时使用剖视面。",
         "点击按当前默认方向定位，保持缩放。", "上视图不依赖文件导入导出；底板面/剖视面需要对应平面。"},
        {CancelTask, "请求取消当前后台任务。", "点击后等待任务安全停止。",
         "需要正在运行且支持取消的任务；已完成的编辑请使用撤销。"},
        {StartRecording, "将之后成功执行的命令逐条写入脚本，包括界面操作对应的命令。",
         "点击并指定 .py 文件后开始；文件已存在时覆盖并打印日志。每条命令立即写入，无需另行保存。",
         "当前未录制且无任务执行；录制期间仍可执行其他脚本，其命令也会被录制。"},
        {StopRecording, "结束录制并关闭脚本文件，已录制内容保留。",
         "录制中点击；无需再执行保存。", "需要正在录制，且当前命令/脚本已执行完成。"},
        {ExecuteScript, "按顺序执行 .py 脚本中的建模命令和 Python 语句。",
         "点击选择脚本；命令窗口逐条显示执行命令及日志。录制中执行的命令也会写入录制文件。",
         "需等待当前任务结束；脚本中途出错时停止，已完成的操作不会自动回滚。"},
    };
    for (const auto &spec : specs)
    {
        setActionHelp(actions.get(spec.id), QString::fromUtf8(spec.purpose), QString::fromUtf8(spec.operation),
                      QString::fromUtf8(spec.requirement));
    }
}
} // namespace reverse::workbench
