# Official base digest recorded from a successful pull, 2026-10-23.
ARG ROS_BASE=ros@sha256:566b87e6dfc48392189a9c32e594e84060fa9948de93b04e536f39475a1f6963
FROM ${ROS_BASE} AS build
SHELL ["/bin/bash", "-c"]
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake libcurl4-openssl-dev python3-colcon-common-extensions && rm -rf /var/lib/apt/lists/*
WORKDIR /opt/rmtc/contestant_ws
COPY contestant_ws/src/rmtc_bot src/rmtc_bot
RUN source /opt/ros/humble/setup.bash && CMAKE_BUILD_PARALLEL_LEVEL=2 MAKEFLAGS=-j2 colcon build --packages-select rmtc_bot --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF

FROM ${ROS_BASE} AS runtime
SHELL ["/bin/bash", "-c"]
RUN dpkg-query -W libcurl4 >/dev/null && useradd --uid 1000 --create-home bot && mkdir /data && chown bot:bot /data
WORKDIR /opt/rmtc
COPY --from=build /opt/rmtc/contestant_ws/install contestant_ws/install
COPY tools tools
COPY transport transport
COPY run_contestant.sh prepare_match.sh decision_config.json source-manifest.json ./
COPY container-entrypoint.sh ./
RUN chmod 755 /opt/rmtc/*.sh
ENV RMTC_LOG_FILE=/data/contestant.jsonl RMW_IMPLEMENTATION=rmw_fastrtps_cpp
USER bot
STOPSIGNAL SIGINT
ENTRYPOINT ["/opt/rmtc/container-entrypoint.sh"]
CMD ["/run/rmtc/match-settings.private.json"]
