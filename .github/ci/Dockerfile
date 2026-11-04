# SPDX-License-Identifier: GPL-3.0-or-later
FROM archlinux:base-20260927.0.600689@sha256:b21322c663be387c0ed9cbc7bbbfe18e41633ad4e7b7c77cfad45f128be20040

COPY packages.arch install.sh /ci/
RUN bash /ci/install.sh

ENV LANG=C.UTF-8 HOME=/home/builder
WORKDIR /workspace
USER builder
