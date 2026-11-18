# Vole-PSI

volePSI implements standard private set intersection (PSI) and circuit PSI, where the result is secret shared between the parties. It is based on [VOLE-PSI: Fast OPRF and Circuit-PSI from Vector-OLE](https://eprint.iacr.org/2021/266) and [Blazing Fast PSI from Improved OKVS and Subfield VOLE](misc/blazingFastPSI.pdf).

The C++ library builds on Windows, Linux, and macOS. Its main dependencies include [libOTe](https://github.com/osu-crypto/libOTe) and [Coproto](https://github.com/ladnir/coproto).

## Build

The build requires Python 3, CMake 3.15 or newer, a C++20 compiler, and Git. `build.py` fetches dependencies as needed.

```sh
git clone https://github.com/ladnir/volepsi.git
cd volepsi
python3 build.py
```

For TCP networking support, build with Boost enabled:

```sh
python3 build.py -DVOLE_PSI_ENABLE_BOOST=ON
```

The library and `frontend` executable are written under `out/build/<platform>/`.

## Use

Run `frontend` without arguments for file-based PSI, example, benchmark, and unit-test options. The [socket example](frontend/networkSocketExample.h) shows network setup; the [message-passing example](frontend/messagePassingExample.h) shows how to exchange protocol messages in an application.

The `-okvs baxos|ribbon` option selects the OKVS backend. Baxos is the default; both parties must select the same backend.

## Install and link

Install volePSI and its fetched dependencies to a chosen prefix:

```sh
python3 build.py --install=path/to/prefix
```

In a CMake consumer, set `CMAKE_PREFIX_PATH` to that prefix and link the exported target:

```cmake
find_package(volePSI REQUIRED)
target_link_libraries(myProject PRIVATE visa::volePSI)
```

## Build options

Pass CMake options to `build.py` with `-D`, for example `python3 build.py -DVOLE_PSI_ENABLE_BOOST=ON`. Common options include `VOLE_PSI_ENABLE_OPENSSL` for TLS support, `VOLE_PSI_ENABLE_CPSI` for circuit PSI, `FETCH_AUTO` for dependency fetching, and `CMAKE_PREFIX_PATH` for installed dependencies. See [build options](cmake/buildOptions.cmake) and `python3 build.py --help` for the full list.
