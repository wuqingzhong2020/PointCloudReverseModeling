list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/service_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/project_history_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/project_operation_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/application_log_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/commandManager/command_manager_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/commandManager/command_service_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/model_display_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/model_editing_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/plate_recognition_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/scan_profile_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/observed_web_holes_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/cloud_editing_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/selection_rectangle_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/SelectionRectangle.cpp")
list(APPEND RM_HEARDERS "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/SelectionRectangle.h")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/viewport_picking_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/point_measurement_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/measurement_snap_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/MeasurementSnap.cpp")
list(APPEND RM_HEARDERS "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/MeasurementSnap.h")
list(APPEND RM_HEARDERS "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/OccMeasurementPicking.h")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/PointMeasurement.cpp")
list(APPEND RM_HEARDERS "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/PointMeasurement.h")
list(APPEND RM_HEARDERS "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/OccPicking.h")
list(APPEND RM_HEARDERS "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/OccPartMesh.h")
# 仅测试中立显示数据的分组与标注，不引入 Qt / AIS / Dock 依赖。
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/ModelDisplayData.cpp")
list(APPEND RM_HEARDERS "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/ModelDisplayData.h")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/ModelLabelVisibility.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/ModelFocus.cpp")
list(APPEND RM_HEARDERS "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/ModelFocus.h")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/model_focus_tests.cpp")
list(APPEND RM_HEARDERS "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/ModelLabelVisibility.h")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/PointCloudDisplayData.cpp")
list(APPEND RM_HEARDERS "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/PointCloudDisplayData.h")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/file_exchange_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/commandManager/startup_arguments_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/task_output_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/OccPartMesh.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/ReferencePlanePicking.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/reference_plane_tests.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/model_transform_tests.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/edge_treatment_tests.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/plate_hole_tests.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/plate_thickness_tests.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/plate_contour_tests.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/thickness_label_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/thickness_label_editor_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/ThicknessLabelEditor.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/ThicknessLabel.cpp")

list(APPEND RM_SOURCES ${CMAKE_CURRENT_LIST_DIR}/plate_generation_tests.cpp)
list(APPEND RM_SOURCES ${CMAKE_CURRENT_LIST_DIR}/structural_steel_tests.cpp)
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/cross_plate_tests.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/mirror_completion_tests.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/local_reconstruction_tests.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/plate_editing_tests.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/tbar_recognition_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/other_model_recognition_tests.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/obstacle_reconstruction_tests.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/end_cap_tests.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/point_modeling_tests.cpp")

# 障碍物向导使用实际 Qt 控件与服务验证选面、框选、预览及错误状态。
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/point_modeling_ui_tests.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/PointModelingTool.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/OccViewport.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/OccViewportTools.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/OccViewportInput.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/OccViewportNavigation.cpp")
list(APPEND RM_HEARDERS "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/OccViewportState.h")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/OccModelPresentation.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/OccReferencePlanePresentation.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/OccMeasurementPresentation.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/OccTransformPreview.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/DockWorkspace.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/WorkbenchTheme.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/WorkbenchActions.cpp")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/WorkbenchActionHelp.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/EditorActionPolicy.cpp")
list(APPEND RM_HEARDERS "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/EditorActionPolicy.h")
list(APPEND RM_HEARDERS "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/workbench/WorkbenchActionId.h")
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/editor_action_policy_tests.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/model_presentation_tests.cpp")

list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/export_transaction_tests.cpp")
