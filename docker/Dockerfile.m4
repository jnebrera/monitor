FROM alpine:3.6

# envsubst (gettext is very big! install only binary & dependency)
RUN apk add --no-cache libintl gettext && \
        cp /usr/bin/envsubst /usr/local/bin/envsubst && \
        apk del gettext

# Common C runtime libraries
RUN apk add --no-cache librdkafka jansson zlib

# n2k libraries
RUN apk add --no-cache yajl libmicrohttpd libev

define(builddeps,bash build-base ca-certificates librdkafka-dev \
		libarchive-tools zlib-dev openssl cgdb valgrind \
		bsd-compat-headers git m4 file guile-dev cmocka-dev)dnl
dnl
ifelse(version,devel,
RUN apk add --no-cache builddeps && \
	apk add --no-cache \
		--repository \
		http://dl-cdn.alpinelinux.org/alpine/edge/testing/ lcov && \
	update-ca-certificates && \
	wget -q -O - \
		https://github.com/eugpermar/xml-coreutils/archive/master.zip \
		| bsdtar -xf- && \
		(cd xml-coreutils-master; bash ./configure --prefix=/usr/local \
			; make; make install) && \
		rm -rfv xml-coreutils-master,
COPY releasefiles /app/
COPY mibfiles /usr/local/share/snmp/mibs/
ENTRYPOINT /app/monitor_setup.sh)


# ca-certificates: for wget bootstrapping
# ncurses - expat: deps for xml-coreutils

WORKDIR /app
