FROM alpine:3.7

# envsubst (gettext is very big! install only binary & dependency)
RUN apk add --no-cache libintl gettext && \
        cp /usr/bin/envsubst /usr/local/bin/envsubst && \
        apk del gettext

# Common C runtime libraries
RUN apk add --no-cache librdkafka zlib

# ca-certificates: for wget bootstrapping
# ncurses - expat: deps for xml-coreutils
# net-snmp-libs only for MIBS
define(builddeps,bash build-base ca-certificates librdkafka-dev \
		libarchive-tools zlib-dev openssl cgdb valgrind \
		bsd-compat-headers git m4 file guile-dev \
		ncurses-dev expat-dev slang-dev python3-dev py3-snmp py3-pytest \
		net-snmp-libs)dnl
dnl
ifelse(version,devel,`
RUN apk add --no-cache builddeps
RUN apk add --no-cache \
		--repository \
		http://dl-cdn.alpinelinux.org/alpine/edge/testing/ lcov
RUN pip3 install --no-cache-dir pykafka pytest-xdist
RUN update-ca-certificates
RUN wget -q -O - \
		https://github.com/eugpermar/xml-coreutils/archive/master.zip \
		| bsdtar -xf- && \
		(cd xml-coreutils-master; bash ./configure --prefix=/usr; \
			chmod +x config/install-sh; make; make install) && \
		rm -rfv xml-coreutils-master
RUN [ "/bin/bash", "-c", "ln -vs /usr/{,local/}share/snmp" ]
ENV PYTEST py.test-3
ENV PYTEST_JOBS 4
ENTRYPOINT ["/bin/bash", "-c"]
CMD ["/bin/bash"]',
COPY releasefiles /app/
COPY mibfiles /usr/local/share/snmp/mibs/
ENTRYPOINT /app/monitor_setup.sh)

WORKDIR /app
