FROM alpine:3.22.6@sha256:5291449c3df73caf6ed85e649dec1b9e818b39a5d8c871e97afc13e9cd5e8fa8
RUN apk add --no-cache make alpine-sdk openmp-dev linux-headers expat-dev expat-static openssl-dev openssl-libs-static
RUN addgroup -S -g 12345 shakti;adduser -G shakti -u 12345 -h /shakti -D shakti
USER 12345:12345
COPY Makefile /shakti
COPY src/ /shakti/src/
# Keep the Makefile's CFLAGS/LDFLAGS (optimisation + hardening: PIE, full
# RELRO, noexecstack, FORTIFY, stack protector); append extras via
# EXTRA_CFLAGS / EXTRA_LDFLAGS rather than overriding CFLAGS.
# X11-backed modules (gfx/synth) are not built here.
RUN make -C /shakti prod SHAKTI_GFX=0 SHAKTI_SYNTH=0 EXTRA_LDFLAGS=-static-pie
USER root
RUN cd /shakti && install -t /bin -o root -g root -m 0755 shakti
COPY lib /lib/shakti/.
ENV SHAKTI_LIB=/lib/shakti
USER 12345:12345
ENTRYPOINT []
