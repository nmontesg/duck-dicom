PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=dicom
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

GARAGE_ENDPOINT = http://localhost:3900
GARAGE_ADMIN_ENDPOINT = http://localhost:3903
GARAGE_ACCESS_KEY = GARAGEADMINACCESSKEY
GARAGE_SECRET_KEY = garagetestingsecretkey
GARAGE_BUCKET = dicom-test
GARAGE_PROFILE = garage-testing
DATA_SOURCE = ./test/test_data/garage_data

ORTHANC_USER = test_user
ORTHANC_PWD = test_pwd
ORTHANC_URL   = http://localhost:8042
ORTHANC_TMP_SEND_DATA = test/test_data/orthanc_data.zip

download_test_data:
	dvc pull

configure_aws_profile:
	aws configure set aws_access_key_id $(GARAGE_ACCESS_KEY) --profile $(GARAGE_PROFILE)
	aws configure set aws_secret_access_key $(GARAGE_SECRET_KEY) --profile $(GARAGE_PROFILE)

setup_garage: download_test_data configure_aws_profile stop_garage
	podman run -d \
		--name garage \
		-p 3900:3900 -p 3903:3903 \
		-v ./test/test_data/garage.toml:/etc/garage.toml \
		-e GARAGE_DEFAULT_ACCESS_KEY=$(GARAGE_ACCESS_KEY) \
		-e GARAGE_DEFAULT_SECRET_KEY=$(GARAGE_SECRET_KEY) \
		-e GARAGE_DEFAULT_BUCKET=$(GARAGE_BUCKET) \
		dxflrs/garage:v2.3.0 \
		/garage server --single-node --default-bucket

	@echo "Waiting for Garage to start..."
	@until curl -s $(GARAGE_ADMIN_ENDPOINT)/health; do sleep 1; done

	aws --endpoint-url $(GARAGE_ENDPOINT) --profile $(GARAGE_PROFILE) \
		s3 sync $(DATA_SOURCE) s3://$(GARAGE_BUCKET)/test_dicom

stop_garage:
	podman rm -f -v garage

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
	podman run -d \
		--name orthanc \
		-p 4242:4242 \
		-p 8042:8042 \
		-v ./test/tls/orthanc:/etc/share/orthanc/tls \
		--add-host=host.docker.internal:host-gateway \
		-e ORTHANC__REGISTERED_USERS="{\"$(ORTHANC_USER)\":\"$(ORTHANC_PWD)\"}" \
		-e ORTHANC__DICOM_TLS_ENABLED=true \
		-e ORTHANC__DICOM_TLS_CERTIFICATE=/etc/share/orthanc/tls/orthanc.crt \
		-e ORTHANC__DICOM_TLS_PRIVATE_KEY=/etc/share/orthanc/tls/orthanc.key \
		-e ORTHANC__DICOM_TLS_TRUSTED_CERTIFICATES=/etc/share/orthanc/tls/trusted.crt \
		-e ORTHANC__DICOM_ALWAYS_ALLOW_FIND=true \
		-e ORTHANC__DICOM_ALWAYS_ALLOW_MOVE=true \
		-e VOLVIEW_PLUGIN_ENABLED=true \
		orthancteam/orthanc:latest

	@echo "Waiting for Orthanc REST API to become available..."
	@until curl -s -f -u $(ORTHANC_USER):$(ORTHANC_PWD) $(ORTHANC_URL)/system > /dev/null; do sleep 2; done

	(cd ./test/test_data/garage_data && zip -r ../orthanc_data.zip .)
	curl -X POST -u $(ORTHANC_USER):$(ORTHANC_PWD) $(ORTHANC_URL)/instances --data-binary @$(ORTHANC_TMP_SEND_DATA)
	rm $(ORTHANC_TMP_SEND_DATA)

	curl -X PUT -u test_user:test_pwd \
  		-d '{"AET":"DUCKDB", "Host":"host.docker.internal", "Port":11112, "UseDicomTls":true}' \
  		$(ORTHANC_URL)/modalities/MOVESCU

stop_orthanc:
	podman rm -f -v orthanc

setup_test_services: setup_garage setup_orthanc
