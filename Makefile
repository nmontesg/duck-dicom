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

ORTHANC_USER = test_user
ORTHANC_PWD = test_pwd
ORTHANC_URL   = http://localhost:8042
ORTHANC_TMP_SEND_DATA = test/test_data/orthanc_data.zip

download_test_data:
	dvc pull

configure_aws_profile:
	aws configure set aws_access_key_id $(MINIO_ACCESS_KEY) --profile $(MINIO_PROFILE)
	aws configure set aws_secret_access_key $(MINIO_SECRET_KEY) --profile $(MINIO_PROFILE)

setup_minio: download_test_data configure_aws_profile stop_minio
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
		s3 sync $(DATA_SOURCE) s3://dicom-test/test_dicom

stop_minio:
	docker rm -f -v minio

generate_tls_certs:
	mkdir -p test/tls/orthanc test/tls/duckdb

	openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
		-keyout test/tls/orthanc/orthanc.key -out test/tls/orthanc/orthanc.crt \
		-subj "/CN=localhost"

	openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
		-keyout test/tls/duckdb/duckdb.key -out test/tls/duckdb/duckdb.crt \
		-subj "/CN=localhost"

	cat test/tls/duckdb/duckdb.crt > test/tls/orthanc/trusted.crt
	cat test/tls/orthanc/orthanc.crt > test/tls/duckdb/trusted.crt

setup_orthanc: generate_tls_certs stop_orthanc
	docker run -d \
		--name orthanc \
		-p 4242:4242 \
		-p 8042:8042 \
		-v ./test/tls/orthanc:/etc/share/orthanc/tls \
		-e ORTHANC__REGISTERED_USERS="{\"$(ORTHANC_USER)\":\"$(ORTHANC_PWD)\"}" \
		-e ORTHANC__DICOM_TLS_ENABLED=true \
		-e ORTHANC__DICOM_TLS_CERTIFICATE=/etc/share/orthanc/tls/orthanc.crt \
		-e ORTHANC__DICOM_TLS_PRIVATE_KEY=/etc/share/orthanc/tls/orthanc.key \
		-e ORTHANC__DICOM_TLS_TRUSTED_CERTIFICATES=/etc/share/orthanc/tls/trusted.crt \
		orthancteam/orthanc:latest

	@echo "Waiting for Orthanc REST API to become available..."
	@until curl -s -f -u $(ORTHANC_USER):$(ORTHANC_PWD) $(ORTHANC_URL)/system > /dev/null; do sleep 2; done

	(cd ./test/test_data/minio_data && zip -r ../orthanc_data.zip .)
	curl -X POST -u $(ORTHANC_USER):$(ORTHANC_PWD) $(ORTHANC_URL)/instances --data-binary @$(ORTHANC_TMP_SEND_DATA)
	rm $(ORTHANC_TMP_SEND_DATA)

stop_orthanc:
	docker rm -f -v orthanc
