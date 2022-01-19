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

  // if (exec_result.er != eevm::ExitReason::returned)
  // {
  //   // Print the trace if nothing was returned
  //   std::cerr << fmt::format("Trace:\n{}", tr) << std::endl;
  //   if (exec_result.er == eevm::ExitReason::threw)
  //   {
  //     // Rethrow to highlight any exceptions raised in execution
  //     throw std::runtime_error(
  //       fmt::format("Execution threw an error: {}", exec_result.exmsg));
  //   }

  //   throw std::runtime_error("Deployment did not return");
  // }

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
    std::cout << "Deployed code ";
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
    
  std::cout << "Caller balance after call: "<< env.gs.get(caller_address).acc.get_balance();
  nlohmann::json j_global_state;
  to_json(j_global_state, *(dynamic_cast<eevm::SimpleGlobalState*>(&env.gs)));
  std::cout << "New global state: " << j_global_state;
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
    const uint256_t total_supply = 1000000;
    eevm::SimpleGlobalState gs;

    // std::vector<uint8_t> code;
    // std::vector<uint8_t> account_storage;
    
    // contract constructor should include all init data
    auto contract_constructor = eevm::to_bytes("0x608060405234801561001057600080fd5b50610178806100206000396000f300608060405260043610610057576000357c0100000000000000000000000000000000000000000000000000000000900463ffffffff16806312065fe01461005c5780633ccfd60b14610087578063d0e30db01461009e575b600080fd5b34801561006857600080fd5b506100716100c0565b6040518082815260200191505060405180910390f35b34801561009357600080fd5b5061009c6100df565b005b6100a6610143565b604051808215151515815260200191505060405180910390f35b60003073ffffffffffffffffffffffffffffffffffffffff1631905090565b3373ffffffffffffffffffffffffffffffffffffffff166108fc6107d03073ffffffffffffffffffffffffffffffffffffffff1631019081150290604051600060405180830381858888f19350505050158015610140573d6000803e3d6000fd5b50565b600060019050905600a165627a7a723058203af50ab840a139cea1f1cdfc17bfd13b0d635533cd093bad92e1428938139f690029");
    append_argument(contract_constructor, total_supply);
    eevm::print_hex(contract_constructor.data(), contract_constructor.size());
    Environment env{
      gs, 
      eevm::from_big_endian(sender, 20u), 
      eevm::from_big_endian(lastHash, 32u), 
      {}
    };
    // deploy(env)

    uint8_t contract_balance[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x0f,0x42,0x40};

    eevm::Address deployed_address = deploy(
      (std::byte*)sender, 
      (std::byte*)lastHash, 
      (std::byte*) &contract_balance, 
      (std::byte*)contract_constructor.data(),
      contract_constructor.size()
    );
    std::cout << deployed_address << std::endl;

    // // test call
    uint8_t contract_address[20u] = {};
    std::memcpy(contract_address, &deployed_address, 20);


    auto deployed_code = eevm::to_bytes("608060405260043610610057576000357c0100000000000000000000000000000000000000000000000000000000900463ffffffff16806312065fe01461005c5780633ccfd60b14610087578063d0e30db01461009e575b600080fd5b34801561006857600080fd5b506100716100c0565b6040518082815260200191505060405180910390f35b34801561009357600080fd5b5061009c6100df565b005b6100a6610143565b604051808215151515815260200191505060405180910390f35b60003073ffffffffffffffffffffffffffffffffffffffff1631905090565b3373ffffffffffffffffffffffffffffffffffffffff166108fc6107d03073ffffffffffffffffffffffffffffffffffffffff1631019081150290604051600060405180830381858888f19350505050158015610140573d6000803e3d6000fd5b50565b600060019050905600a165627a7a723058203af50ab840a139cea1f1cdfc17bfd13b0d635533cd093bad92e1428938139f690029");
    auto function_call =
      // eevm::to_bytes("d0e30db0"); // deposit
      eevm::to_bytes("3ccfd60b"); // withdraw

    append_argument(function_call, eevm::from_big_endian(sender, 20u));

    std::string str_storage = "{}";
    auto rs = call(
      (std::byte*)sender, 
      (std::byte*)lastHash, 
      (std::byte*) contract_address, 
      (std::byte*) &contract_balance, 
      str_storage.c_str(), 
      (std::byte*)deployed_code.data(),
      (uint)deployed_code.size(),  
      (std::byte*)function_call.data(),
      (uint)function_call.size()
    );

    std::cout << "Balance: " << eevm::from_big_endian(rs.data(), rs.size()) << std::endl;

    // auto function_call =
    //     eevm::to_bytes("70a08231");

    // auto mycaller = eevm::to_bytes("e3f32c4b6b86e018c8c6fed0653da210c8363705");
    // append_argument(function_call, eevm::from_big_endian(mycaller.data(), 20u));
    // eevm::print_hex(function_call.data(), function_call.size());

    // auto myRs = eevm::to_bytes("00000000000000000000000000000000000000000000000000000000000f4240");
    // std::cout << "Balance: " << eevm::from_big_endian(myRs.data(), myRs.size()) << std::endl;


  // auto transfer_function_call = eevm::to_bytes("a9059cbb");
  // auto target_address =  eevm::to_bytes("f854c27c46e3fbf2abbacd29ec4aff517369c667");
  // std::cout << "Transfer to address: ";

  // uint8_t byte_target_address [20u] = {};
  // std::memcpy(byte_target_address, target_address.data(), target_address.size());
  // eevm::print_hex(byte_target_address, 20u);
  
  
  // const uint256_t transfer_amount = 1000;
  // // append_argument(transfer_function_call, eevm::from_big_endian(mycaller.data(), 20u));
  // append_argument(transfer_function_call, eevm::from_big_endian(target_address.data(), 20u));
  // append_argument(transfer_function_call, transfer_amount);
  // std::cout << "transfer hex data: ";
  // eevm::print_hex(transfer_function_call.data(), transfer_function_call.size());


  //  auto query_receiver_function_call =
  //       eevm::to_bytes("70a08231");

  //   append_argument(query_receiver_function_call,  eevm::from_big_endian(target_address.data(), 20u));
  //   eevm::print_hex(query_receiver_function_call.data(), query_receiver_function_call.size());
}

