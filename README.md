# Vole-PSI


Vole-PSI implements the protocols described in [VOLE-PSI: Fast OPRF and Circuit-PSI from Vector-OLE](https://eprint.iacr.org/2021/266) and [Blazing Fast PSI from Improved OKVS and Subfield VOLE](misc/blazingFastPSI.pdf). The library implements standard [Private Set Intersection (PSI)](https://en.wikipedia.org/wiki/Private_set_intersection) along with a variant called Circuit PSI where the result is secret shared between the two parties.

The library is cross platform (win,linux,mac) and depends on [libOTe](https://github.com/osu-crypto/libOTe), [sparsehash](https://github.com/sparsehash/sparsehash), [Coproto](https://github.com/Visa-Research/coproto).

### Build

The library can be cloned and built with networking support as
```
git clone https://github.com/Visa-Research/volepsi.git
cd volepsi
python3 build.py -DVOLE_PSI_ENABLE_BOOST=ON
```
If TCP/IP support is not required, then a minimal version of the library can be build by calling `python3 build.py`. See below and the cmake/python output for additional options.
The user can manually call cmake as well.

When upgrading from an older checkout, run `python3 build.py --clean` before rebuilding to refresh the staged dependencies. The bundled libOTe revision is pinned in `thirdparty/getLibOTe.cmake`; externally supplied libOTe builds must provide the same API. Update both PSI peers together: volePSI now selects `ChosenBaa16x8` over GF(2^128), and older protocol builds are not wire-compatible. Silver and QuasiCyclic are not supported for the GF128 VOLE used by PSI.

The new VOLE encoder uses the chosen-block BAA construction with RS[16,8,9], full-field nonzero weights, and uniform shuffles. Each call adds a joint code-seed exchange (three messages, 64 payload bytes), including in reduced-round mode. The field arithmetic and PSI interfaces remain GF(2^128). Use `setMultType(oc::MultType::ChosenBaa16x4)` for the quarter-rate RS[16,4,13] profile; both peers must select the same profile.

The paper's finite code-sampling certificates apply at specific lengths and to ideal randomness. The implementation uses pseudo-distance 0.30 for RS[16,8,9] and 0.60 for RS[16,4,13] as engineering estimates across supported lengths. At the default 128-bit target, the half-rate profile selects 128 noise positions and the quarter-rate profile selects 64. Their independent attack floors are `secParam` and `ceil(secParam / 2)`, respectively; the distance requirement can raise either count. These choices do not claim a new proof of 128-bit VOLE security for every configuration. See the pinned libOTe revision's `libOTe/Tools/BlkAccCode/README.md` for the ensemble, seed exchange, and security scope.

The output library `volePSI` and executable `frontend` will be written to `out/build/<platform>/`. The `frontend` can perform PSI based on files as input sets and communicate via sockets. See the output of `frontend` for details. There is also two example on how to perform [networking](https://github.com/Visa-Research/volepsi/blob/main/frontend/networkSocketExample.h#L7) or [manually](https://github.com/Visa-Research/volepsi/blob/main/frontend/messagePassingExample.h#L93) get & send the protocol messages.

##### Compile Options
Options can be set as `-D NAME=VALUE`. For example, `-D VOLE_PSI_NO_SYSTEM_PATH=true`. See the output of the build for default/current value. Options include :
 * `VOLE_PSI_NO_SYSTEM_PATH`, values: `true,false`.  When looking for dependencies, do not look in the system install. Instead use `CMAKE_PREFIX_PATH` and the internal dependency management.  
* `CMAKE_BUILD_TYPE`, values: `Debug,Release,RelWithDebInfo`. The build type. 
* `FETCH_AUTO`, values: `true,false`. If true, dependencies will first be searched for and if not found then automatically downloaded.
* `FETCH_SPARSEHASH`, values: `true,false`. If true, the dependency sparsehash will always be downloaded. 
* `FETCH_LIBOTE`, values: `true,false`. If true, the dependency libOTe will always be downloaded. 
* `FETCH_LIBDIVIDE`, values: `true,false`. If true, the dependency libdivide will always be downloaded. 
* `VOLE_PSI_ENABLE_SSE`, values: `true,false`. If true, the library will be built with SSE intrinsics support. 
* `VOLE_PSI_ENABLE_PIC`, values: `true,false`. If true, the library will be built `-fPIC` for shared library support. 
* `VOLE_PSI_ENABLE_ASAN`, values: `true,false`. If true, the library will be built ASAN enabled. 
* `VOLE_PSI_ENABLE_GMW`, values: `true,false`. If true, the GMW protocol will be compiled. Only used for Circuit PSI.
* `VOLE_PSI_ENABLE_CPSI`, values: `true,false`. If true,  the circuit PSI protocol will be compiled. 
* `VOLE_PSI_ENABLE_OPPRF`, values: `true,false`.  If true, the OPPRF protocol will be compiled. Only used for Circuit PSI.
* `VOLE_PSI_ENABLE_BOOST`, values: `true,false`. If true, the library will be built with boost networking support. This support is managed by libOTe. 
* `VOLE_PSI_ENABLE_OPENSSL`, values: `true,false`. If true,the library will be built with OpenSSL networking support. This support is managed by libOTe. If enabled, it is the responsibility of the user to install openssl to the system or to a location contained in `CMAKE_PREFIX_PATH`.
* `VOLE_PSI_ENABLE_BITPOLYMUL`, values: `true,false`. Enables libOTe's binary quasi-cyclic OT support. It does not select the GF(2^128) VOLE encoder used by PSI.
* `VOLE_PSI_ENABLE_SODIUM`, values: `true,false`. If true, the library will be built libSodium for doing elliptic curve operations. This or relic must be enabled. This support is managed by libOTe. 
* `VOLE_PSI_SODIUM_MONTGOMERY`, values: `true,false`. If true, the library will use a non-standard version of sodium that enables slightly better efficiency. 
* `VOLE_PSI_ENABLE_RELIC`, values: `true,false`. If true, the library will be built relic for doing elliptic curve operations. This or sodium must be enabled. This support is managed by libOTe. 


### Installing

The library and any fetched dependencies can be installed. 
```
python3 build.py --install
```
or 
```
python3 build.py --install=install/prefix/path
```
if a custom install prefix is perfected. Install can also be performed via cmake.

### Linking

libOTe can be linked via cmake as
```
find_package(volepsi REQUIRED)
target_link_libraries(myProject visa::volepsi)
```
To ensure that cmake can find volepsi, you can either install volepsi or build it locally and set `-D CMAKE_PREFIX_PATH=path/to/volepsi` or provide its location as a cmake `HINTS`, i.e. `find_package(volepsi HINTS path/to/volepsi)`.

To link a non-cmake project you will need to link volepsi, libOTe,coproto, macoro, (sodium or relic), optionally boost and openss if enabled. These will be installed to the install location and staged to `./out/install/<platform>`. 


### Dependency Management

By default the dependencies are fetched automatically. This can be turned off by using cmake directly or adding `-D FETCH_AUTO=OFF`. For other options see the cmake output or that of `python build.py --help`.

If the dependency is installed to the system, then cmake should automatically find it if `VOLE_PSI_NO_SYSTEM_PATH` is `false`. If they are installed to a specific location, then you call tell cmake about them as 
```
python3 build.py -D CMAKE_PREFIX_PATH=install/prefix/path
```

