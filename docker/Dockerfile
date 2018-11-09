FROM alpine:3.7 as mon-base

# Common C runtime libraries
RUN apk add --no-cache librdkafka zlib

WORKDIR /app

#
# Devel-base: Only compilation tools
#

FROM mon-base as mon-dev-base

RUN apk add --no-cache \
	bash \
	bsd-compat-headers \
	ca-certificates \
	file \
	gcc \
	git \
	guile-dev \
	libarchive-tools \
	librdkafka-dev \
	m4 \
	make \
	musl-dev \
	openssl \
	zlib-dev

#
# Devel: Devel-base + tools like valgrind, cgdb, pytest...
#
FROM mon-dev-base as mon-dev

# ca-certificates: for wget bootstrapping
# ncurses - expat: deps for xml-coreutils
# net-snmp-libs only for MIBS
RUN apk add --no-cache \
	cgdb \
	expat-dev \
	file \
	musl-dbg \
	ncurses-dev \
	net-snmp-libs \
	py3-pytest \
	py3-snmp \
	python3-dev \
	slang-dev

RUN apk add --no-cache \
		--repository \
		http://dl-cdn.alpinelinux.org/alpine/edge/testing/ lcov
RUN apk add --no-cache \
		--repository \
		http://dl-cdn.alpinelinux.org/alpine/edge/main/ valgrind
RUN pip3 install --upgrade pip && pip3 install --no-cache-dir pykafka pytest-xdist
RUN update-ca-certificates
RUN wget -q -O - \
		https://github.com/eugpermar/xml-coreutils/archive/master.zip \
		| bsdtar -xf- && \
		(cd xml-coreutils-master; bash ./configure --prefix=/usr; \
			chmod +x config/install-sh; make; make install) && \
		rm -rfv xml-coreutils-master

ENV PYTEST py.test-3
ENV PYTEST_JOBS 4

# Make our life easy
RUN [ "/bin/bash", "-c", "ln -vs /usr/{,local/}share/snmp" ]

#
# Monitor builder: Release image actual builder
#

FROM mon-dev-base AS mon-builder

ARG CONFIGURE_OPTIONS=''

COPY . /app
RUN env CC=gcc ./configure --bootstrap $CONFIGURE_OPTIONS
RUN make mibs
RUN make

# Delete all non-mibs stuff
RUN find vendor/net_snmp/net_snmp/mibs/ -name '*.txt' -o -type d -o -delete

#
# Release
#

FROM mon-base AS release

# envsubst (gettext is very big! install only binary & dependency)
RUN apk add --no-cache libintl gettext && \
        cp /usr/bin/envsubst /usr/local/bin/envsubst && \
        apk del gettext

COPY --from=mon-builder /app/vendor/net_snmp/net_snmp/mibs/ \
	/usr/local/share/snmp/mibs/
COPY --from=mon-builder  \
	/app/docker/release/config.json.env \
	/app/docker/release/monitor_setup.sh \
	/app/Makefile.config \
	/app/rb_monitor \
	/app/

ENTRYPOINT ["/app/monitor_setup.sh"]
