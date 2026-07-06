FROM alpine
RUN apk add --no-cache make alpine-sdk openmp-dev linux-headers rdma-core-dev expat-dev
RUN addgroup -S -g 12345 shakti;adduser -G shakti -u 12345 -h /shakti -D shakti
USER 12345:12345
COPY src /shakti/src
COPY Makefile /shakti/Makefile
RUN cd /shakti && make prod-speed SHAKTI_SYNTH=0
USER root
RUN cd /shakti && install -t /bin -o root -g root -m 0755 shakti
RUN mkdir -p /bin/src && cp -r /shakti/src/lib /bin/src/ && cp /shakti/shakti /bin/.
USER 12345:12345
ENTRYPOINT []
