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


uint256_t deploy(Environment& env, std::vector<uint8_t> contract_constructor) {
    // contract_constructor include contract code and contructor argument in it
    const auto contract_address = eevm::generate_contract_address(env.owner_address, env.owner_last_hash);

    // TODO: may check if account have any data ?? or just skip and let node do it?

    // Set this constructor as the contract's code body
    auto contract = env.gs.create(contract_address, 0u, contract_constructor);

    // Run a transaction to initialise this account
    auto result =
        run_and_check_result(env, env.owner_address, contract_address, {});

    // Result of running the compiled constructor is the code that should be the
    // contract's body (constructor will also have setup contract's Storage)
    contract.acc.set_code(std::move(result));
    std::vector<uint8_t> code = contract.acc.get_code(); 
    return contract.acc.get_address();
} 

eevm::AccountState init_contract_with_storage(Environment& env, eevm::Address contract_address, uint256_t balance, eevm::Code& code, const nlohmann::json& j_storage) {
  return env.gs.create_with_storage(contract_address, balance, code, j_storage);
}

// std::string call(string old_storage, eevm::Address contract_address) { 
//     return "ok";
// }

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

uint256_t get_balance(
  Environment& env,
  const eevm::Address& contract_address,
  const eevm::Address& target_address)
{
  auto function_call =
    eevm::to_bytes("70a08231");

  append_argument(function_call, target_address);

  const auto output =
    run_and_check_result(env, target_address, contract_address, function_call);

  return eevm::from_big_endian(output.data(), output.size());
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
    auto contract_constructor = eevm::to_bytes("608060405234801561001057600080fd5b5060405160208061040783398101604090815290516000818155338152600160205291909120556103c1806100466000396000f3006080604052600436106100775763ffffffff7c0100000000000000000000000000000000000000000000000000000000600035041663095ea7b3811461007c57806318160ddd146100b457806323b872dd146100db57806370a0823114610105578063a9059cbb14610126578063dd62ed3e1461014a575b600080fd5b34801561008857600080fd5b506100a0600160a060020a0360043516602435610171565b604080519115158252519081900360200190f35b3480156100c057600080fd5b506100c96101d8565b60408051918252519081900360200190f35b3480156100e757600080fd5b506100a0600160a060020a03600435811690602435166044356101de565b34801561011157600080fd5b506100c9600160a060020a03600435166102c4565b34801561013257600080fd5b506100a0600160a060020a03600435166024356102df565b34801561015657600080fd5b506100c9600160a060020a036004358116906024351661036a565b336000818152600260209081526040808320600160a060020a038716808552908352818420869055815186815291519394909390927f8c5be1e5ebec7d5bd14f71427d1e84f3dd0314c0f7b2291e5b200ac8c7c3b925928290030190a35060015b92915050565b60005490565b600160a060020a03831660009081526001602052604081205482118015906102295750600160a060020a03841660009081526002602090815260408083203384529091529020548211155b156102b957600160a060020a038085166000818152600160209081526040808320805488900390559387168083528483208054880190559282526002815283822033808452908252918490208054879003905583518681529351929391927fddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef9281900390910190a35060016102bd565b5060005b9392505050565b600160a060020a031660009081526001602052604090205490565b3360009081526001602052604081205482116103625733600081815260016020908152604080832080548790039055600160a060020a03871680845292819020805487019055805186815290519293927fddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef929181900390910190a35060016101d2565b5060006101d2565b600160a060020a039182166000908152600260209081526040808320939094168252919091522054905600a165627a7a7230582048867a831c7e97d2f7475a6a0c393fabe9c04714f8255cb9995d0531763eb2fb0029");
    append_argument(contract_constructor, total_supply);
    Environment env{
      gs, 
      eevm::from_big_endian(sender, 20u), 
      eevm::from_big_endian(lastHash, 32u), 
      {}
    };
    // deploy(env)
    eevm::Address deployed_address = deploy(env, contract_constructor);
    std::cout << deployed_address << std::endl;


    // init with storage
    eevm::SimpleGlobalState gs2;
    Environment env2{
      gs2, 
      eevm::from_big_endian(sender, 20u), 
      eevm::from_big_endian(lastHash, 32u), 
      {}
    };

    auto deployed_code = env.gs.get(deployed_address).acc.get_code();
    nlohmann::json json_storage = env.gs.get(deployed_address).st.get_storage();
    init_contract_with_storage(env2, deployed_address, 0, deployed_code, json_storage);
    // init_contract_with_storage(env2, deployed_address, 0, contract_constructor, json_storage);


    //get storage
    // eevm::SimpleStorage storage = (eevm::SimpleStorage)env.gs.get(deployed_address).st;
    // std::cout << json_storage;

    std::cout << get_balance(env2, deployed_address, env.owner_address);
}