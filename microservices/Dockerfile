# One image per service: docker build --build-arg SERVICE=<name> -f microservices/Dockerfile .
# (build context = repo root, so the scripts the service runs come from fincept-qt/scripts)
FROM mirror.gcr.io/library/python:3.11-slim
ARG SERVICE
ENV FINCEPT_SERVICE=${SERVICE} \
    FINCEPT_ROUTES=/app/routes.json \
    FINCEPT_SCRIPTS_DIR=/app/scripts \
    FINCEPT_DATA_DIR=/data \
    PIP_DISABLE_PIP_VERSION_CHECK=1
WORKDIR /app
COPY microservices/runner/requirements.txt /tmp/runner-requirements.txt
COPY microservices/${SERVICE}/requirements.txt /tmp/service-requirements.txt
RUN pip install --no-cache-dir -r /tmp/runner-requirements.txt -r /tmp/service-requirements.txt
COPY fincept-qt/scripts /app/scripts
COPY microservices/runner /app/runner
COPY microservices/routes.json /app/routes.json
RUN useradd --create-home --uid 10001 fincept && mkdir -p /data && chown fincept /data
USER fincept
WORKDIR /app/runner
EXPOSE 8000
HEALTHCHECK --interval=10s --timeout=3s --start-period=60s \
  CMD python -c "import urllib.request,sys; sys.exit(0 if urllib.request.urlopen('http://127.0.0.1:8000/healthz').status==200 else 1)"
CMD ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "8000", "--no-access-log"]
