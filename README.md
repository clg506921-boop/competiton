# RMTC 参赛程序与提交材料（v0.25.1-final 封版）

当前代码基线：v0.25.1-tail-offer-fix，C++指纹1bf25f43ac4992484c5e。正式队名：肥鱼说的都队；队长：蔡礼格；队伍编号：12。最终交付版本v0.25.1-final，容器内工程路径/opt/rmtc。最新优化候选未混入本仓库。

## 目录

- contestant_ws/src/rmtc_bot/：C++程序、头文件、ROS入口、测试及内置JSON依赖。
- tools/：启动配置、只读预检、离线核验及开发工具。
- transport/：本机练习UDP配置；现场有线比赛不使用回环限制。
- prepare_match.sh、run_contestant.sh：统一入口。
- decision_config.json：预置决策类型，当前balanced。
- docs/submission/：本轮技术报告、参数卡、检录与提交清单。
- source-manifest.json：当前草稿源文件校验清单。
- baseline-source-manifest.json：原v0.25.1验证基线清单。

历史docs中的旧说明仅作研发参考。历史大日志已在本地另行归档；仓库保留完整程序源码及docs/submission/证据中的必要验证附件。现场指令以本README、docs/submission及主办方最新通知核对，不使用过期示例地址作为现场分配值。

## 环境与依赖

Ubuntu22.04、ROS2 Humble、Linux/amd64、C++17、Python3；构建依赖ament_cmake、rclcpp、std_msgs、Threads、CMake/colcon、libcurl开发库及C++编译器。JSON头文件已放在vendor目录，不依赖现场下载。GTest只在开发测试时使用，以下参赛构建关闭测试目标。

依赖应在赛前装好。使用其他CPU架构、系统或ROS版本需自行适配并重新验收，不能直接套用已有二进制哈希。

## 从源码构建

```bash
cd /绝对路径/rmtc-submission
source /opt/ros/humble/setup.bash
CMAKE_BUILD_PARALLEL_LEVEL=1 MAKEFLAGS=-j1 colcon build \
  --base-paths contestant_ws/src --build-base contestant_ws/build \
  --install-base contestant_ws/install --packages-select rmtc_bot \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
```

预期产物：contestant_ws/install/rmtc_bot/lib/rmtc_bot/turing_bot_node。准备脚本会加载同一工程的ROS与install环境。重新构建后的产物需在实际运行路径核验，不把其他工作空间的同名节点当作本次版本。

## 两阶段启动

先把docs/submission中的模板复制到仓库外私有目录，填写本场接入信息。第一阶段A席、100ms/100Tick；第二阶段按本场席位、500ms/500Tick。模板包含null，未填写完整时预检会失败。

```bash
bash prepare_match.sh --check /绝对路径/stage1.private.json
bash prepare_match.sh --launch /绝对路径/stage1.private.json
```

第一阶段程序停止后，第二阶段：

```bash
bash prepare_match.sh --check /绝对路径/stage2.private.json
bash prepare_match.sh --launch /绝对路径/stage2.private.json
```

stage字段分别为字符串"1"、"2"，不是另起参考车。每个席位只保留一个本队控制实例。正常停止用Ctrl+C。正式异常重启应先按裁判流程登记。

## 配置与运行逻辑

本场参数使用单个私有JSON：seat、stage、team_name、referee_host、ros_domain_id、network、gateway_url、team_token、model、max_tokens、ai_quota、max_ticks、tick_ms。正式有线及默认裁判使用wired；local仅用于双方均匹配的回环UDP配置。本次同机最终检录使用wired/default DDS完成。模型deepseek-flash、max_tokens=512按参赛手册填写；本场额度尚需主办方提供。令牌不提交Git，禁止在截图、报告或公开日志中展示。

感知与动作话题随席位选择为/team_<seat>/perception和/team_<seat>/action，JSON内tick/session_id来自该帧感知，位置来自self.position。动作只有经过本车后续self.last_action核对，才算完成执行闭环。LLM工作线程只产生经校验的事件信息，不直接发布过期动作。

## 当前提交状态

最终比赛版本已确定为v0.25.1-final，底层v0.25.1-tail-offer-fix；代码与镜像已冻结。两阶段练习端最终镜像检录证据已补齐：第一阶段100/100、40分；第二阶段mock500/500、45分第一名、97次攻击，均0无效动作。数字感知坐标、场次、动作和后续回执截图已加入报告6.3节。尚未代为向赛事入口正式提交。

注册话题存在待核对项：参赛手册写/team_<seat>/registration，当前v1.1练习基线仍使用/referee/registration。需在正式现场接入前向主办方确认正式裁判是否兼容当前注册话题；本版本仅证明官方v1.1练习端通过，未宣称正式裁判接口验收通过。不得把该风险写成已解决。

最终镜像练习端检录图与历史截图已按版本分别注明；工作人员协作权限和正式提交回执尚需实际办理，见docs/submission清单。

## 材料入口

- 技术报告：[语雀导入稿](docs/submission/01-技术报告-语雀导入稿.md)
- 语雀文档：https://www.yuque.com/ahz56/cbis6h/nlgzqv1hvhksyz2c（正文及截图已同步；工作人员权限仍需核验）
- 当前仓库经队员确认公开上传；正式令牌和私有配置不得提交。

## 最终封版交付

代码已按队员要求冻结，不再混入后续候选。镜像与代码身份见 [release/release-identity.json](release/release-identity.json)。容器入口说明及最终脚本见 [release/start-final.sh](release/start-final.sh)。镜像包含原始Docker归档，文件较大，单独交付，不写入Git仓库。正式比赛参数和密钥使用包外私有配置。

源码锚点提交：a8571dcccc9a931d94db555a85572203cb5ffb22；当前仓库后续提交为材料和封版说明更新，不改变生产代码。
