#pragma once
namespace fincept::python {
/// --selftest-services: drives the real PythonRunner against the services
/// gateway in FINCEPT_SERVICES_URL (see microservices/). Needs the stack running.
/// FINCEPT_SELFTEST_DOWN_SERVICE=<name> asserts that service fails cleanly
/// while the others keep answering. Returns process exit code (0 = all pass).
int run_services_selftest();
} // namespace fincept::python
