#include "eEVM/opcode.h"
#include "eEVM/processor.h"
#include "eEVM/simple/simpleglobalstate.h"

#include <cassert>
#include <fmt/format_header_only.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <vector>

struct Environment
{
  eevm::GlobalState& gs;
  const eevm::Address& owner_address;
  const uint256_t& owner_last_hash;
  std::vector<uint8_t> code;
};

// Run input as an EVM transaction, check the result and return the output
std::vector<uint8_t> run_and_check_result(
  Environment& env,
  const eevm::Address& from,
  const eevm::Address& to,
  const eevm::Code& input)
{
  // Ignore any logs produced by this transaction
  eevm::NullLogHandler ignore;
  eevm::Transaction tx(from, ignore);

  // Record a trace to aid debugging
  eevm::Trace tr;
  eevm::Processor p(env.gs);

  // Run the transaction
  const auto exec_result = p.run(tx, from, env.gs.get(to), input, 0u, &tr);
  std::cout << "return result" << (int)exec_result.er << std::endl;
  if (exec_result.er != eevm::ExitReason::returned)
  {
    // Print the trace if nothing was returned
    std::cerr << fmt::format("Trace:\n{}", tr) << std::endl;
    if (exec_result.er == eevm::ExitReason::threw)
    {
      // Rethrow to highlight any exceptions raised in execution
      throw std::runtime_error(
        fmt::format("Execution threw an error: {}", exec_result.exmsg));
    }

    throw std::runtime_error("Deployment did not return");
  }

  return exec_result.output;
}


uint256_t deploy(
  std::byte* b_caller_address, 
  std::byte* b_caller_last_hash, 
  std::byte* b_contract_balance, 
  std::byte* b_contract_constructor,
  uint contract_constructor_length
) {

   // format argument to right data type
    uint256_t caller_address = eevm::from_big_endian((uint8_t *)b_caller_address, 20u);
    uint256_t caller_last_hash = eevm::from_big_endian((uint8_t *)b_caller_last_hash, 32u);
    uint256_t contract_balance = eevm::from_big_endian((uint8_t *)b_contract_balance, 32u);
    std::vector<uint8_t> contract_constructor((uint8_t*)b_contract_constructor, (uint8_t*)b_contract_constructor + contract_constructor_length);

    eevm::SimpleGlobalState gs;
    //  init env
    Environment env{
      gs, 
      caller_address,
      caller_last_hash, 
      {}
    };

    // contract_constructor include contract code and contructor argument in it
    const auto contract_address = eevm::generate_contract_address(env.owner_address, env.owner_last_hash);

    // TODO: may check if account have any data ?? or just skip and let node do it?
    std::cout << contract_balance << std::endl;
    // Set this constructor as the contract's code body
    auto contract = env.gs.create(contract_address, 0u, contract_constructor);

    // Run a transaction to initialise this account
    auto result =
        run_and_check_result(env, env.owner_address, contract_address, {});

    // Result of running the compiled constructor is the code that should be the
    // contract's body (constructor will also have setup contract's Storage)
    contract.acc.set_code(std::move(result));
    

    std::vector<uint8_t> code = contract.acc.get_code(); 
    std::cout << "CODE" << std::endl;
    eevm::print_hex(code.data(), code.size());
  
    std::cout << "Storage" << std::endl;
    nlohmann::json json_storage = contract.st.get_storage();
    std::cout << json_storage << std::endl;
    
    return contract.acc.get_address();
} 

eevm::AccountState init_contract_with_storage(
  Environment& env, 
  eevm::Address contract_address, 
  uint256_t balance, 
  eevm::Code& code, 
  const nlohmann::json& j_storage
) {
  return env.gs.create_with_storage(contract_address, balance, code, j_storage);
}

std::vector<uint8_t> call(
  std::byte* b_caller_address, 
  std::byte* b_caller_last_hash, 
  std::byte* b_contract_address, 
  std::byte* b_contract_balance, 
  const char * old_storage, 
  std::byte* code,
  uint code_length,  
  std::byte* input,
  uint input_length
) { 
    // format argument to right data type
    uint256_t caller_address = eevm::from_big_endian((uint8_t *)b_caller_address, 20u);
    uint256_t caller_last_hash = eevm::from_big_endian((uint8_t *)b_caller_last_hash, 32u);
    uint256_t contract_address = eevm::from_big_endian((uint8_t *)b_contract_address, 20u);
    uint256_t contract_balance = eevm::from_big_endian((uint8_t *)b_contract_balance, 32u);
    nlohmann::json j_storage = nlohmann::json::parse((std::string)old_storage);
    std::vector<uint8_t> vector_code((uint8_t*)code, (uint8_t*)code + code_length);
    std::vector<uint8_t> vector_input((uint8_t*)input, (uint8_t*)input + input_length);

    eevm::SimpleGlobalState gs;
    //  init env
    Environment env{
      gs, 
      caller_address,
      caller_last_hash, 
      {}
    };

    // init contract and contract storage
    init_contract_with_storage(
      env,  
      contract_address, 
      contract_balance, 
      vector_code, 
      j_storage
    );

  const auto output =
    run_and_check_result(env, caller_address, contract_address, vector_input);
  
  return output;
}

void append_argument(std::vector<uint8_t>& code, const uint256_t& arg)
{
  // To ABI encode a function call with a uint256_t (or Address) argument,
  // simply append the big-endian byte representation to the code (function
  // selector, or bin). ABI-encoding for more complicated types is more
  // complicated, so not shown in this sample.
  const auto pre_size = code.size();
  code.resize(pre_size + 32u);
  eevm::to_big_endian(arg, code.data() + pre_size);
}

uint256_t get_random_uint256(size_t bytes = 32)
{
  std::vector<uint8_t> raw(bytes);
  std::generate(raw.begin(), raw.end(), []() { return rand(); });
  return eevm::from_big_endian(raw.data(), raw.size());
}

eevm::Address get_random_address()
{
  return get_random_uint256(20);
}

int main(int argc, char** argv)
{
    uint8_t sender[20] = {80, 121, 129, 142, 250, 76, 105, 165, 252, 52, 227, 37, 89, 169, 249, 66, 131, 227, 18, 132};
    uint8_t lastHash[32] = {78, 3, 101, 122, 234, 69, 169, 79, 199, 212, 123, 168, 38, 200, 214, 103, 192, 209, 230, 227, 58, 100, 160, 54, 236, 68, 245, 143, 161, 45, 108, 70};
    eevm::SimpleGlobalState gs;

    // std::vector<uint8_t> code;
    // std::vector<uint8_t> account_storage;
    
    // contract constructor should include all init data
    auto contract_constructor = eevm::to_bytes("608060405234801561001057600080fd5b5060038054600160a060020a03191633179055600060018190556002556102ef8061003c6000396000f30060806040526004361061008d5763ffffffff7c01000000000000000000000000000000000000000000000000000000006000350416630b93381b811461009257806312424e3f146100bb57806369cacdaf146100d05780637e45e942146100e55780638da5cb5b1461010c57806396a097461461014a578063e69dda1c14610178578063f1d10573146101a6575b600080fd5b34801561009e57600080fd5b506100a76101bb565b604080519115158252519081900360200190f35b3480156100c757600080fd5b506100a76101c4565b3480156100dc57600080fd5b506100a7610221565b3480156100f157600080fd5b506100fa61022a565b60408051918252519081900360200190f35b34801561011857600080fd5b50610121610230565b6040805173ffffffffffffffffffffffffffffffffffffffff9092168252519081900360200190f35b34801561015657600080fd5b506100a773ffffffffffffffffffffffffffffffffffffffff6004351661024c565b34801561018457600080fd5b506100fa73ffffffffffffffffffffffffffffffffffffffff600435166102ab565b3480156101b257600080fd5b506100fa6102bd565b60005460ff1681565b3360009081526004602052604081205415156101df57600080fd5b336000908152600460205260409020600290819055805460019081019182905554111561020b57600080fd5b506000805460ff19166001179081905560ff1690565b60005460ff1690565b60015481565b60035473ffffffffffffffffffffffffffffffffffffffff1681565b60035460009073ffffffffffffffffffffffffffffffffffffffff16331461027357600080fd5b5073ffffffffffffffffffffffffffffffffffffffff1660009081526004602052604081206001908190558054810190555460ff1690565b60046020526000908152604090205481565b600254815600a165627a7a72305820e232452ce99b9f5cae90b704b0ac02e197dd35fe272b0ac9ac267bcf7d4865c00029");
    eevm::print_hex(contract_constructor.data(), contract_constructor.size());


    uint8_t contract_balance[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

    
    eevm::Address deployed_address = deploy(
      (std::byte*)sender, 
      (std::byte*)lastHash, 
      (std::byte*) &contract_balance, 
      (std::byte*)contract_constructor.data(),
      contract_constructor.size()
    );
  
    
    // // test call
    uint8_t contract_address[20u] = {};
    std::memcpy(contract_address, &deployed_address, 20);

    auto function_call =
      eevm::to_bytes("96a09746");
      

    append_argument(function_call, eevm::from_big_endian(sender, 20u));
    eevm::print_hex(function_call.data(), function_call.size());

    std::string str_storage = "{\"0x3\":\"0x5079818efa4c69a5fc34e32559a9f94283e31284\"}";

    std::cout << "Start call: " << std::endl;
    auto rs = call(
      (std::byte*)sender, 
      (std::byte*)lastHash, 
      (std::byte*) contract_address, 
      (std::byte*) &contract_balance, 
      str_storage.c_str(), 
      (std::byte*)contract_constructor.data(),
      (uint)contract_constructor.size(),  
      (std::byte*)function_call.data(),
      (uint)function_call.size()
    );

    std::cout << "rs size" << rs.size();
    eevm::print_hex(rs.data(), rs.size());

}

