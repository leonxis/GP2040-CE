function (compile_proto)
	find_package(Python3 REQUIRED COMPONENTS Interpreter)

	set(VENV ${CMAKE_CURRENT_BINARY_DIR}/venv)
	set(VENV_FILE ${VENV}/environment.txt)
	if(CMAKE_HOST_WIN32)
		set(VENV_BIN_DIR ${VENV}/Scripts)
	else()
		set(VENV_BIN_DIR ${VENV}/bin)
	endif()

	# Check if git proxy is set and extract SOCKS proxy info
	set(GIT_PROXY "")
	execute_process(
		COMMAND git config --global --get http.proxy
		OUTPUT_VARIABLE GIT_PROXY
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)

	# Set up pip install commands with proxy support if needed
	if(GIT_PROXY)
		# Extract proxy from git config (format: socks5://host:port)
		# pip needs http_proxy and https_proxy for SOCKS proxy, and PySocks must be installed first
		# Use system pip (which already has PySocks) to install PySocks into venv, then use proxy for other packages
		# Get Python version at configure time to determine site-packages path
		execute_process(
			COMMAND ${Python3_EXECUTABLE} -c "import sys; print('{}.{}'.format(sys.version_info.major, sys.version_info.minor))"
			OUTPUT_VARIABLE PYTHON_VERSION
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)
		set(VENV_SITE_PACKAGES ${VENV}/lib/python${PYTHON_VERSION}/site-packages)
		add_custom_command(
			DEPENDS ${CMAKE_SOURCE_DIR}/lib/nanopb/extra/requirements.txt
			COMMAND ${Python3_EXECUTABLE} -m venv ${VENV}
			COMMAND ${Python3_EXECUTABLE} -m pip install --target ${VENV_SITE_PACKAGES} PySocks
			COMMAND ${CMAKE_COMMAND} -E env http_proxy=${GIT_PROXY} https_proxy=${GIT_PROXY} ${VENV_BIN_DIR}/pip install --upgrade pip
			COMMAND ${CMAKE_COMMAND} -E env http_proxy=${GIT_PROXY} https_proxy=${GIT_PROXY} ${VENV_BIN_DIR}/pip --disable-pip-version-check install -r ${CMAKE_SOURCE_DIR}/lib/nanopb/extra/requirements.txt
			COMMAND ${VENV_BIN_DIR}/pip freeze > ${VENV_FILE}
			OUTPUT ${VENV_FILE}
			COMMENT "Setting up Python Virtual Environment with proxy"
		)
	else()
		add_custom_command(
			DEPENDS ${CMAKE_SOURCE_DIR}/lib/nanopb/extra/requirements.txt
			COMMAND ${Python3_EXECUTABLE} -m venv ${VENV}
			COMMAND ${VENV_BIN_DIR}/pip install --upgrade pip
			COMMAND ${VENV_BIN_DIR}/pip install PySocks
			COMMAND ${VENV_BIN_DIR}/pip --disable-pip-version-check install -r ${CMAKE_SOURCE_DIR}/lib/nanopb/extra/requirements.txt
			COMMAND ${VENV_BIN_DIR}/pip freeze > ${VENV_FILE}
			OUTPUT ${VENV_FILE}
			COMMENT "Setting up Python Virtual Environment"
		)
	endif()

	set(NANOPB_GENERATOR ${CMAKE_SOURCE_DIR}/lib/nanopb/generator/nanopb_generator.py)
	set(PROTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto)
	set(PROTO_OUTPUT_DIR ${PROTO_OUTPUT_DIR} PARENT_SCOPE)

	add_custom_command(
		DEPENDS ${VENV_FILE} ${NANOPB_GENERATOR} ${CMAKE_SOURCE_DIR}/proto/enums.proto ${CMAKE_SOURCE_DIR}/proto/config.proto ${CMAKE_SOURCE_DIR}/lib/nanopb/generator/proto/nanopb.proto
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${PROTO_OUTPUT_DIR}
		COMMAND ${VENV_BIN_DIR}/python ${NANOPB_GENERATOR}
			-q
			-D ${PROTO_OUTPUT_DIR}
			-I ${CMAKE_SOURCE_DIR}/proto
			-I ${CMAKE_SOURCE_DIR}/lib/nanopb/generator/proto
			${CMAKE_SOURCE_DIR}/proto/enums.proto
		COMMAND ${VENV_BIN_DIR}/python ${NANOPB_GENERATOR}
			-q
			-D ${PROTO_OUTPUT_DIR}
			-I ${CMAKE_SOURCE_DIR}/proto
			-I ${CMAKE_SOURCE_DIR}/lib/nanopb/generator/proto
			${CMAKE_SOURCE_DIR}/proto/config.proto
		OUTPUT ${PROTO_OUTPUT_DIR}/config.pb.c ${PROTO_OUTPUT_DIR}/config.pb.h ${PROTO_OUTPUT_DIR}/enums.pb.c ${PROTO_OUTPUT_DIR}/enums.pb.h
		COMMENT "Compiling enums.proto and config.proto"
	)
endfunction()
