#!/usr/bin/make

DOCKER_BIN := $(shell command -v docker 2> /dev/null)

DOCKERFILE := .ci/Dockerfile
IMAGE_NAME := drmhwc_ci

# We can't run style and bpfmt check in docker
# when repo is within AOSP tree, will run it locally.
GIT_IS_SYMLINK:=$(shell test -L .git && echo true)

define print_no_docker_err
$(warning Please install docker, e.g. for Ubuntu:)
$(warning $$ sudo apt install docker.io)
$(warning $$ sudo usermod -aG docker $$USER)
$(warning and reboot your PC)
$(error Aborting...)
endef

.PHONY : help prepare shell ci ci_cleanup local_presubmit local_cleanup
.DEFAULT_GOAL := help

help: ## Show this help
	@awk 'BEGIN {FS = ":.*?## "} /^[a-zA-Z_-]+:.*?## / {printf "  \033[36m%-15s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)

PREPARE:=.out/prepare_docker.timestamp
$(PREPARE): $(DOCKERFILE)
	$(if $(DOCKER_BIN),,$(call print_no_docker_err))
	mkdir -p $(dir $@)
	$(DOCKER_BIN) build -t local/build-env -f $(DOCKERFILE) .;
	$(DOCKER_BIN) stop $(IMAGE_NAME) || true
	$(DOCKER_BIN) rm $(IMAGE_NAME) || true
	$(DOCKER_BIN) run -itd --name $(IMAGE_NAME) --network="host" -v $(shell pwd):/home/user/drm_hwcomposer local/build-env
	@touch $@

prepare: $(PREPARE)
prepare: ## Build and run Docker image

shell: $(PREPARE)
shell: ## Start shell into a container
	$(DOCKER_BIN) exec -it $(IMAGE_NAME) bash

ci: $(PREPARE)
ci: ## Run presubmit within the docker container
	@echo "Run native build:"
	$(DOCKER_BIN) exec -it $(IMAGE_NAME) bash -c "make -f .ci/Makefile -j12"
	@echo "Run style check:"
	$(if $(GIT_IS_SYMLINK), \
		./.ci/.gitlab-ci-checkcommit.sh, \
		$(DOCKER_BIN) exec -it $(IMAGE_NAME) bash -c "./.ci/.gitlab-ci-checkcommit.sh")
	@echo "\n\e[32m --- SUCCESS ---\n"

ci_cleanup: ## Cleanup after 'make ci'
	$(DOCKER_BIN) exec -it $(IMAGE_NAME) bash -c "make local_cleanup"

local_presubmit: ## Run local presubmit script (requires latest Ubuntu + additional packages). Consider 'make ci' instead
	@echo "Run native build:"
	make -f .ci/Makefile -j12
	@echo "Run style check:"
	./.ci/.gitlab-ci-checkcommit.sh
	@echo "\n\e[32m --- SUCCESS ---\n"

local_cleanup: ## Cleanup after 'make local_presubmit'
	make -f .ci/Makefile clean
