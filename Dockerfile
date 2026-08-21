FROM alpine
RUN apk add --no-cache make alpine-sdk openmp-dev linux-headers expat-dev expat-static
RUN addgroup -S -g 12345 shakti;adduser -G shakti -u 12345 -h /shakti -D shakti
USER 12345:12345
COPY Makefile /shakti
COPY src/ /shakti/src/
RUN make -C /shakti prod CFLAGS=-static
USER root
RUN cd /shakti && install -t /bin -o root -g root -m 0755 shakti
COPY lib /lib/shakti/.
USER 12345:12345
ENTRYPOINT []
