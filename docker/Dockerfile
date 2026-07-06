# Ubuntu 20.04 (Focal) build image for WifiGD Linux binaries.
# Produces shared libraries linked against glibc 2.31 for broad Linux compatibility.
FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
	&& apt-get install -y --no-install-recommends \
		build-essential \
		ca-certificates \
		git \
		libnm-dev \
		pkg-config \
		python3 \
		python3-pip \
	&& pip3 install --no-cache-dir "scons>=4.0,<5" \
	&& rm -rf /var/lib/apt/lists/*

WORKDIR /workspace