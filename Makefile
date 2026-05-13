PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=dicom
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

MINIO_ENDPOINT = http://localhost:9000
MINIO_ACCESS_KEY = admin
MINIO_SECRET_KEY = password123
MINIO_PROFILE = minio-testing
DATA_SOURCE = ./test/test_data/minio_data

download_test_data:
	dvc pull

configure_aws_profile:
	aws configure set aws_access_key_id $(MINIO_ACCESS_KEY) --profile $(MINIO_PROFILE)
	aws configure set aws_secret_access_key $(MINIO_SECRET_KEY) --profile $(MINIO_PROFILE)

setup_minio: download_test_data configure_aws_profile
	docker run -d \
		--name minio \
		-p 9000:9000 \
		-p 9001:9001 \
		-e MINIO_ROOT_USER=$(MINIO_ACCESS_KEY) \
		-e MINIO_ROOT_PASSWORD=$(MINIO_SECRET_KEY) \
		quay.io/minio/minio:latest server --console-address ":9001" /data

	@echo "Waiting for MinIO to start..."
	@until curl -s $(MINIO_ENDPOINT)/minio/health/live; do sleep 1; done

	aws --endpoint-url $(MINIO_ENDPOINT) --profile $(MINIO_PROFILE) \
		s3 mb s3://dicom-test

	aws --endpoint-url $(MINIO_ENDPOINT) --profile $(MINIO_PROFILE) \
		s3 sync $(DATA_SOURCE) s3://dicom-test/2_skull_ct

stop_minio:
	docker rm -f -v minio
