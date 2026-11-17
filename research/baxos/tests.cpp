#include "tests/Paxos_Tests.h"
#include <iostream>
int main(int argc,char** argv) {try {oc::CLP cmd; cmd.parse(argc,argv);
Baxos_randomized_parallel_determinism_Test(cmd);std::cout<<"PASS Baxos_randomized_parallel_determinism_Test\n"<<std::flush;
Paxos_dense_kernel_Test(cmd);std::cout<<"PASS Paxos_dense_kernel_Test\n"<<std::flush;
Paxos_mod32_Test(cmd);std::cout<<"PASS Paxos_mod32_Test\n"<<std::flush;
Baxos_dense_reference_Test(cmd);std::cout<<"PASS Baxos_dense_reference_Test\n"<<std::flush;
Paxos_buildRow_Test(cmd);std::cout<<"PASS Paxos_buildRow_Test\n"<<std::flush;
Paxos_solve_Test(cmd);std::cout<<"PASS Paxos_solve_Test\n"<<std::flush;
Paxos_solve_u8_Test(cmd);std::cout<<"PASS Paxos_solve_u8_Test\n"<<std::flush;
Paxos_solve_mtx_Test(cmd);std::cout<<"PASS Paxos_solve_mtx_Test\n"<<std::flush;
Paxos_invE_Test(cmd);std::cout<<"PASS Paxos_invE_Test\n"<<std::flush;
Paxos_invE_g3_Test(cmd);std::cout<<"PASS Paxos_invE_g3_Test\n"<<std::flush;
Paxos_solve_gap_Test(cmd);std::cout<<"PASS Paxos_solve_gap_Test\n"<<std::flush;
Paxos_solve_rand_Test(cmd);std::cout<<"PASS Paxos_solve_rand_Test\n"<<std::flush;
Paxos_solve_rand_gap_Test(cmd);std::cout<<"PASS Paxos_solve_rand_gap_Test\n"<<std::flush;
Baxos_solve_Test(cmd);std::cout<<"PASS Baxos_solve_Test\n"<<std::flush;
Baxos_solve_mtx_Test(cmd);std::cout<<"PASS Baxos_solve_mtx_Test\n"<<std::flush;
Baxos_solve_par_Test(cmd);std::cout<<"PASS Baxos_solve_par_Test\n"<<std::flush;
Baxos_solve_rand_Test(cmd);std::cout<<"PASS Baxos_solve_rand_Test\n"<<std::flush;
Baxos_bin_overflow_Test(cmd);std::cout<<"PASS Baxos_bin_overflow_Test\n"<<std::flush;
Baxos_bin_size_Test(cmd);std::cout<<"PASS Baxos_bin_size_Test\n"<<std::flush;
Baxos_decode_weight_overflow_Test(cmd);std::cout<<"PASS Baxos_decode_weight_overflow_Test\n"<<std::flush;
Baxos_idxtype_sentinel_and_params_Test(cmd);std::cout<<"PASS Baxos_idxtype_sentinel_and_params_Test\n"<<std::flush;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
