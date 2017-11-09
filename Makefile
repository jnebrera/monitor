
BIN = rb_monitor

SRCS = $(addprefix src/, \
	main.c rb_snmp.c rb_value.c rb_zk.c rb_monitor_zk.c \
	rb_sensor.c rb_sensor_queue.c rb_array.c rb_sensor_monitor.c \
	rb_sensor_monitor_array.c rb_message_list.c rb_libmatheval.c rb_json.c)
OBJS = $(SRCS:.c=.o)
TESTS_C = $(sort $(wildcard tests/0*.c))
VERSION_H = src/version.h

TESTS = $(TESTS_C:.c=.test)
OBJ_DEPS_TESTS := tests/json_test.o tests/sensor_test.o tests/snmp_test.o
TESTS_OBJS = $(TESTS:.test=.o)
TESTS_CHECKS_XML = $(TESTS_C:.c=.xml)
TESTS_MEM_XML = $(TESTS_C:.c=.mem.xml)
TESTS_HELGRIND_XML = $(TESTS_C:.c=.helgrind.xml)
TESTS_DRD_XML = $(TESTS_C:.c=.drd.xml)
TESTS_VALGRIND_XML = $(TESTS_MEM_XML) $(TESTS_HELGRIND_XML) $(TESTS_DRD_XML)
TESTS_XML = $(TESTS_CHECKS_XML) $(TESTS_VALGRIND_XML)
COV_FILES = $(foreach ext,gcda gcno, $(SRCS:.c=.$(ext)) $(TESTS_C:.c=.$(ext)))

VALGRIND ?= valgrind
CLANG_FORMAT ?= clang-format-3.8
SUPPRESSIONS_FILE ?= tests/valgrind.suppressions
ifneq ($(wildcard $(SUPPRESSIONS_FILE)),)
SUPPRESSIONS_VALGRIND_ARG = --suppressions=$(SUPPRESSIONS_FILE)
endif

all: $(VERSION_H) $(BIN)

include mklove/Makefile.base

# Update binary version if needed
actual_git_version:=$(shell git describe --abbrev=6 --tags HEAD --always)
ifneq (,$(wildcard $(VERSION_H)))
version_c_version:=$(shell sed -n 's/.*n2kafka_version="\([^"]*\)";/\1/p' -- $(VERSION_H))
endif

# Re-make $(VERSION_H) if git version changed
ifneq (,$(filter-out $(actual_git_version),$(GITVERSION) $(version_c_version)))
VERSION_H_PHONY=$(VERSION_H)
$(shell sed -i 's/$(GITVERSION)/$(actual_git_version)/g' -- Makefile.config)
endif

.PHONY: tests checks memchecks drdchecks helchecks coverage \
	check_coverage clang-format-check $(VERSION_H_PHONY)

$(VERSION_H):
	@echo "static const char *monitor_version=\"$(actual_git_version)\";" > $@

clean: bin-clean
	rm -f $(TESTS) $(TESTS_OBJS) $(TESTS_XML) $(COV_FILES) $(OBJ_DEPS_TESTS) \
		$(VERSION_H)

install: bin-install

run_tests = tests/run_tests.sh $(1) $(TESTS_C:.c=)
run_valgrind = echo "$(MKL_YELLOW) Generating $(2) $(MKL_CLR_RESET)" && $(VALGRIND) --tool=$(1) $(SUPPRESSIONS_VALGRIND_ARG) --xml=yes \
					--xml-file=$(2) $(3) >/dev/null 2>&1

clang-format-files = $(wildcard src/*.c src/*.h)
clang-format:
	@for src in $(clang-format-files); do \
		$(CLANG_FORMAT) -i $$src; \
	done

clang-format-check: SHELL=/bin/bash
clang-format-check:
	@set -o pipefail; \
	for src in $(wildcard src/*.c src/*.h); do \
		diff -Nu <($(CLANG_FORMAT) $$src) $$src | colordiff || ERR="yes";  \
	done; if [[ ! -z $$ERR ]]; then false; fi

tests: $(TESTS_XML)
	@$(call run_tests, -cvdh)

checks: $(TESTS_CHECKS_XML)
	@$(call run_tests,-c)

memchecks: $(TESTS_VALGRIND_XML)
	@$(call run_tests,-v)

drdchecks: $(TESTS_DRD_XML)
	@$(call run_tests,-d)

helchecks: $(TESTS_HELGRIND_XML)
	@$(call run_tests,-h)

tests/%.mem.xml: tests/%.test
	-@$(call run_valgrind,memcheck,"$@","./$<")

tests/%.helgrind.xml: tests/%.test
	-@$(call run_valgrind,helgrind,"$@","./$<")

tests/%.drd.xml: tests/%.test
	-@$(call run_valgrind,drd,"$@","./$<")

tests/%.xml: tests/%.test
	@echo "$(MKL_YELLOW) Generating $@$(MKL_CLR_RESET)"
	@CMOCKA_XML_FILE="$@" CMOCKA_MESSAGE_OUTPUT=XML "./$<" >/dev/null 2>&1

tests/%.o: CPPFLAGS := $(filter-out -flto,$(CPPFLAGS))
tests/%.test: CPPFLAGS := -I. $(CPPFLAGS)
MALLOC_FUNCTIONS := $(strip malloc calloc __strdup strdup \
	json_object_new_object printbuf_new evaluator_create \
	json_object_new_string json_object_new_int64 snmp_sess_synch_response \
	snmp_free_pdu)
WRAP_ALLOC_FUNCTIONS := $(foreach fn, $(MALLOC_FUNCTIONS)\
						,-Wl,-u,$(fn) -Wl,-wrap,$(fn))
tests/%.test: tests/%.o $(filter-out src/main.o,$(OBJS)) $(OBJ_DEPS_TESTS)
	$(CC) $(WRAP_ALLOC_FUNCTIONS) $(CPPFLAGS) $(LDFLAGS) $^ -o $@ $(LIBS) -lcmocka

check_coverage:
	@( if [[ "x$(WITH_COVERAGE)" == "xn" ]]; then \
	echo "$(MKL_RED) You need to configure using --enable-coverage"; \
	echo -n "$(MKL_CLR_RESET)"; \
	false; \
	fi)

COVERAGE_INFO ?= coverage.info
COVERAGE_OUTPUT_DIRECTORY ?= coverage.out.html
COV_VALGRIND ?= valgrind
COV_GCOV ?= gcov
COV_LCOV ?= lcov

coverage: check_coverage $(TESTS)
	( for test in $(TESTS); do ./$$test; done )
	$(COV_LCOV) --gcov-tool=$(COV_GCOV) -q \
                --rc lcov_branch_coverage=1 --capture \
                --directory ./ --output-file ${COVERAGE_INFO}
	genhtml --branch-coverage ${COVERAGE_INFO} --output-directory \
				${COVERAGE_OUTPUT_DIRECTORY} > coverage.out
	# ./display_coverage.sh

rpm: clean
	$(MAKE) -C packaging/rpm

DOCKER_OUTPUT_TAG?=gcr.io/wizzie-registry/rb_monitor
DOCKER_OUTPUT_VERSION?=1.1.0-rc

vendor_net_snmp_mib_dir=vendor/net_snmp/net_snmp/mibs
vendor_net_snmp_mib_makefile=$(vendor_net_snmp_mib_dir)/Makefile.mib
# If we have used net-snmp vendors, we will use it to update SNMP
ifneq (,$(wildcard $(vendor_net_snmp_mib_makefile)))
SHELL := /bin/bash
mibs_deps=$(addprefix $(vendor_net_snmp_mib_dir)/, \
                      $(shell make -f $(vendor_net_snmp_mib_makefile) \
                          -f <(echo -e "print-MIBS:\n\t@echo \$$(MIBS)") \
                          print-MIBS))
endif

# else, we will use system default
ifeq (,$(mibs_deps))
mibs_deps=$(wildcard /usr/local/share/snmp/mibs/*.txt)
endif

DOCKER_RELEASE_FILES=$(strip rb_monitor docker/release/config.json.env \
	docker/release/monitor_setup.sh)

$(vendor_net_snmp_mib_dir)/%.txt:
	cd $(vendor_net_snmp_mib_dir); make "$(notdir $@)"

docker: docker/release/Dockerfile \
		$(filter-out rb_monitor,$(DOCKER_RELEASE_FILES)) \
		$(mibs_deps)
	docker build -t $(DOCKER_OUTPUT_TAG):$(DOCKER_OUTPUT_VERSION) \
		-f docker/release/Dockerfile .

dev-docker: docker/devel/Dockerfile
	@docker build $(DOCKER_BUILD_PARAMETERS) docker/devel

docker/release/Dockerfile:RELEASEFILES_ARG=--define=releasefiles='$(DOCKER_RELEASE_FILES)'
docker/release/Dockerfile:MIBS_ARG=--define=mibfiles='$(mibs_deps)'
%/Dockerfile: docker/Dockerfile.m4
	mkdir -p "$(dir $@)"
	m4 $(RELEASEFILES_ARG) $(MIBS_ARG) --define=version="$(@:docker/%/Dockerfile=%)" "$<" > "$@"

-include $(DEPS)
