#include <bts/cli/cli.hpp>
#include <bts/rpc/rpc_server.hpp>
#include <bts/rpc/exceptions.hpp>
#include <bts/wallet/pretty.hpp>
#include <bts/wallet/wallet.hpp>
#include <bts/blockchain/withdraw_types.hpp>

#include <fc/io/buffered_iostream.hpp>
#include <fc/io/console.hpp>
#include <fc/io/json.hpp>
#include <fc/io/sstream.hpp>
#include <fc/log/logger.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/thread/thread.hpp>
#include <fc/variant.hpp>

#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/range/algorithm/max_element.hpp>
#include <boost/range/algorithm/min_element.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <boost/optional.hpp>

#include <iomanip>
#include <iostream>
#include <sstream>
#include <fstream>


#ifdef HAVE_READLINE
# include <readline/readline.h>
# include <readline/history.h>
// I don't know exactly what version of readline we need.  I know the 4.2 version that ships on some macs is
// missing some functions we require.  We're developing against 6.3, but probably anything in the 6.x 
// series is fine
# if RL_VERSION_MAJOR < 6
#  ifdef _MSC_VER
#   pragma message("You have an old version of readline installed that might not support some of the features we need")
#   pragma message("Readline support will not be compiled in")
#  else
#   warning "You have an old version of readline installed that might not support some of the features we need"
#   warning "Readline support will not be compiled in"
#  endif
#  undef HAVE_READLINE
# endif
#endif

namespace bts { namespace cli {

  namespace detail
  {
      class cli_impl
      {
         public:
            bts::client::client*                            _client;
            rpc_server_ptr                                  _rpc_server;
            bts::cli::cli*                                  _self;
            fc::thread                                      _cin_thread;
                                                            
            bool                                            _quit;
            bool                                            show_raw_output;
            bool                                            _daemon_mode;

            boost::iostreams::stream< boost::iostreams::null_sink > nullstream;
            
            std::ostream*                  _saved_out;
            std::ostream*                  _out;   //cout | log_stream | tee(cout,log_stream) | null_stream
            std::istream*                  _command_script;
            std::istream*                  _input_stream;
            boost::optional<std::ostream&> _input_stream_log;
            bool _filter_output_for_tests;


            cli_impl(bts::client::client* client, std::istream* command_script, std::ostream* output_stream);

            void process_commands(std::istream* input_stream);

            void start()
              {
                try
                {
                  if (_command_script)
                    process_commands(_command_script);
                  if (_daemon_mode)
                    _rpc_server->wait_till_rpc_server_shutdown();
                  else if (!_quit)
                    process_commands(&std::cin);
                  _rpc_server->shutdown_rpc_server();
                }
                catch ( const fc::exception& e)
                {
                    *_out << "\nshutting down\n";
                    elog( "${e}", ("e",e.to_detail_string() ) );
                    _rpc_server->shutdown_rpc_server();
                }
              }

            string get_prompt()const
            {
              string wallet_name =  _client->get_wallet()->get_wallet_name();
              string prompt = wallet_name;
              if( prompt == "" )
              {
                 prompt = "(wallet closed) " CLI_PROMPT_SUFFIX;
              }
              else
              {
                 if( _client->get_wallet()->is_locked() )
                    prompt += " (locked) " CLI_PROMPT_SUFFIX;
                 else
                    prompt += " (unlocked) " CLI_PROMPT_SUFFIX;
              }
              return prompt;
            }

            void parse_and_execute_interactive_command(string command, 
                                                       fc::istream_ptr argument_stream )
            { 
              if( command.size() == 0 )
                 return;
              if (command == "enable_raw")
              {
                  show_raw_output = true;
                  return;
              }
              else if (command == "disable_raw")
              {
                  show_raw_output = false;
                  return;
              }

              fc::buffered_istream buffered_argument_stream(argument_stream);

              bool command_is_valid = false;
              fc::variants arguments;
              try
              {
                arguments = _self->parse_interactive_command(buffered_argument_stream, command);
                // NOTE: arguments here have not been filtered for private keys or passwords
                // ilog( "command: ${c} ${a}", ("c",command)("a",arguments) ); 
                command_is_valid = true;
              }
              catch( const rpc::unknown_method& )
              {
                 if( command.size() )
                   *_out << "Error: invalid command \"" << command << "\"\n";
              }
              catch( const fc::canceled_exception&)
              {
                *_out << "Command aborted.\n";
              }
              catch( const fc::exception& e)
              {
                *_out << e.to_detail_string() <<"\n";
                *_out << "Error parsing command \"" << command << "\": " << e.to_string() << "\n";
                arguments = fc::variants { command };
                edump( (e.to_detail_string()) );
                auto usage = _client->help( command ); //_rpc_server->direct_invoke_method("help", arguments).as_string();
                *_out << usage << "\n";
              }

              //if command is valid, go ahead and execute it
              if (command_is_valid)
              {
                try
                {
                  fc::variant result = _self->execute_interactive_command(command, arguments);
                  _self->format_and_print_result(command, arguments, result);
                }
                catch( const fc::canceled_exception&)
                {
                  throw;
                }
                catch( const fc::exception& e )
                {
                  *_out << e.to_detail_string() << "\n";
                }
              }
            } //parse_and_execute_interactive_command

            bool execute_command_line(const string& line)
            { try {
                     wdump( (line));
            
              string trimmed_line_to_parse(boost::algorithm::trim_copy(line));
              /** 
               *  On some OS X systems, std::stringstream gets corrupted and does not throw eof
               *  when expected while parsing the command.  Adding EOF (0x04) characater at the
               *  end of the string casues the JSON parser to recognize the EOF rather than relying
               *  on stringstream.  
               *
               *  @todo figure out how to fix things on these OS X systems.
               */
              trimmed_line_to_parse += string(" ") + char(0x04);
              if (!trimmed_line_to_parse.empty())
              {
                string::const_iterator iter = std::find_if(trimmed_line_to_parse.begin(), trimmed_line_to_parse.end(), ::isspace);
                string command;
                fc::istream_ptr argument_stream;
                if (iter != trimmed_line_to_parse.end())
                {
                  // then there are arguments to this function
                  size_t first_space_pos = iter - trimmed_line_to_parse.begin();
                  command = trimmed_line_to_parse.substr(0, first_space_pos);
                  argument_stream = std::make_shared<fc::stringstream>((trimmed_line_to_parse.substr(first_space_pos + 1)));
                }
                else
                {
                  command = trimmed_line_to_parse;
                  argument_stream = std::make_shared<fc::stringstream>();
                }
                try
                {
                  parse_and_execute_interactive_command(command,argument_stream);
                }
                catch( const fc::canceled_exception& )
                {
                  if( command == "quit" ) 
                    return false;
                  *_out << "Command aborted\n";
                }
              } //end if command line not empty
              return true;
            } FC_RETHROW_EXCEPTIONS( warn, "", ("command",line) ) }


            string get_line( const string& prompt = CLI_PROMPT_SUFFIX, bool no_echo = false)
            {
                  if( _quit ) return std::string();
                  if( _input_stream == nullptr )
                     FC_CAPTURE_AND_THROW( fc::canceled_exception ); //_input_stream != nullptr );

                  //FC_ASSERT( _self->is_interactive() );
                  string line;
                  if ( no_echo )
                  {
                      *_out << prompt;
                      // there is no need to add input to history when echo is off, so both Windows and Unix implementations are same
                      fc::set_console_echo(false);
                      _cin_thread.async([this,&line](){ std::getline( *_input_stream, line ); }).wait();
                      fc::set_console_echo(true);
                      *_out << std::endl;
                  }
                  else
                  {
                  #ifdef HAVE_READLINE 
                    if (_input_stream == &std::cin)
                    {
                      char* line_read = nullptr;
                      _out->flush(); //readline doesn't use cin, so we must manually flush _out
                      line_read = readline(prompt.c_str());
                      if(line_read && *line_read)
                          add_history(line_read);
                      if( line_read == nullptr )
                         FC_THROW_EXCEPTION( fc::eof_exception, "" );
                      line = line_read;
                      free(line_read);
                    }
                  else
                    {
                      *_out <<prompt;
                      _cin_thread.async([this,&line](){ std::getline( *_input_stream, line ); }).wait();
                    }
                  #else
                    *_out <<prompt;
                    _cin_thread.async([this,&line](){ std::getline( *_input_stream, line ); }).wait();
                  #endif
                  if (_input_stream_log)
                    {
                    _out->flush();
                    if (_saved_out)
                      *_input_stream_log << CLI_PROMPT_SUFFIX;
                    *_input_stream_log << line << std::endl;
                    }
                  }

                  boost::trim(line);
                  return line;
            }

            fc::variants parse_interactive_command(fc::buffered_istream& argument_stream, const string& command)
            {
              try
              {
                const bts::api::method_data& method_data = _rpc_server->get_method_data(command);
                return _self->parse_recognized_interactive_command(argument_stream, method_data);
              }
              catch( const fc::key_not_found_exception& )
              {
                return _self->parse_unrecognized_interactive_command(argument_stream, command);
              }
            }

            fc::variants parse_recognized_interactive_command( fc::buffered_istream& argument_stream,
                                                               const bts::api::method_data& method_data)
            { try {
              fc::variants arguments;
              for (unsigned i = 0; i < method_data.parameters.size(); ++i)
              {
                try
                {
                  arguments.push_back(_self->parse_argument_of_known_type(argument_stream, method_data, i));
                }
                catch( const fc::eof_exception&)
                {
                  if (method_data.parameters[i].classification != bts::api::required_positional)
                  {
                    return arguments;
                  }
                  else //if missing required argument, prompt for that argument
                  {
                    const bts::api::parameter_data& this_parameter = method_data.parameters[i];
                    string prompt = this_parameter.name /*+ "(" + this_parameter.type  + ")"*/ + ": ";

                    //if we're prompting for a password, don't echo it to console
                    bool is_new_passphrase = (this_parameter.type == "new_passphrase");
                    bool is_passphrase = (this_parameter.type == "passphrase") || is_new_passphrase;
                    if (is_passphrase)
                    {
                      auto passphrase = prompt_for_input( this_parameter.name, is_passphrase, is_new_passphrase );
                      arguments.push_back(fc::variant(passphrase));
                    }
                    else //not a passphrase
                    {
                      string prompt_answer = get_line(prompt, is_passphrase );
                      auto prompt_argument_stream = std::make_shared<fc::stringstream>(prompt_answer);
                      fc::buffered_istream buffered_argument_stream(prompt_argument_stream);
                      try
                      {
                        arguments.push_back(_self->parse_argument_of_known_type(buffered_argument_stream, method_data, i));
                      }
                      catch( const fc::eof_exception& e )
                      {
                          FC_THROW("Missing argument ${argument_number} of command \"${command}\"",
                                   ("argument_number", i + 1)("command", method_data.name)("cause",e.to_detail_string()) );
                      }
                      catch( fc::parse_error_exception& e )
                      {
                        FC_RETHROW_EXCEPTION(e, error, "Error parsing argument ${argument_number} of command \"${command}\": ${detail}",
                                              ("argument_number", i + 1)("command", method_data.name)("detail", e.get_log()));
                      }
                    } //else not a passphrase
                 } //end prompting for missing required argument
                } //end catch eof_exception
                catch( fc::parse_error_exception& e )
                {
                  FC_RETHROW_EXCEPTION(e, error, "Error parsing argument ${argument_number} of command \"${command}\": ${detail}",
                                        ("argument_number", i + 1)("command", method_data.name)("detail", e.get_log()));
                }

                if (method_data.parameters[i].classification == bts::api::optional_named)
                  break;
              }
              return arguments;
            } FC_CAPTURE_AND_RETHROW() }

            fc::variants parse_unrecognized_interactive_command( fc::buffered_istream& argument_stream,
                                                                 const string& command)
            {
              /* Quit isn't registered with the RPC server, the RPC server spells it "stop" */
              if (command == "quit")
                return fc::variants();

              FC_THROW_EXCEPTION(fc::key_not_found_exception, "Unknown command \"${command}\".", ("command", command));
            }

            fc::variant parse_argument_of_known_type( fc::buffered_istream& argument_stream,
                                                      const bts::api::method_data& method_data,
                                                      unsigned parameter_index)
            { try {
              const bts::api::parameter_data& this_parameter = method_data.parameters[parameter_index];
              if (this_parameter.type == "asset")
              {
                // for now, accept plain int, assume it's always in the base asset
                uint64_t amount_as_int;
                try
                {
                  fc::variant amount_as_variant = fc::json::from_stream(argument_stream);
                  amount_as_int = amount_as_variant.as_uint64();
                }
                catch( fc::bad_cast_exception& e )
                {
                  FC_RETHROW_EXCEPTION(e, error, "Error parsing argument ${argument_number} of command \"${command}\": ${detail}",
                                        ("argument_number", parameter_index + 1)("command", method_data.name)("detail", e.get_log()));
                }
                catch( fc::parse_error_exception& e )
                {
                  FC_RETHROW_EXCEPTION(e, error, "Error parsing argument ${argument_number} of command \"${command}\": ${detail}",
                                        ("argument_number", parameter_index + 1)("command", method_data.name)("detail", e.get_log()));
                }
                return fc::variant(bts::blockchain::asset(amount_as_int));
              }
              else if (this_parameter.type == "address")
              {
                // allow addresses to be un-quoted
                while( isspace(argument_stream.peek()) )
                  argument_stream.get();
                fc::stringstream address_stream;
                try
                {
                  while( !isspace(argument_stream.peek()) )
                    address_stream.put(argument_stream.get());
                }
                catch( const fc::eof_exception& )
                {
                   // *_out << "ignoring eof  line: "<<__LINE__<<"\n";
                   // expected and ignored
                }
                string address_string = address_stream.str();

                try
                {
                  bts::blockchain::address::is_valid(address_string);
                }
                catch( fc::exception& e )
                {
                  FC_RETHROW_EXCEPTION(e, error, "Error parsing argument ${argument_number} of command \"${command}\": ${detail}",
                                        ("argument_number", parameter_index + 1)("command", method_data.name)("detail", e.get_log()));
                }
                return fc::variant( bts::blockchain::address(address_string) );
              }
              else
              {
                // assume it's raw JSON
                try
                {
                  auto tmp = fc::json::from_stream( argument_stream );
                  return  tmp;
                }
                catch( fc::parse_error_exception& e )
                {
                  FC_RETHROW_EXCEPTION(e, error, "Error parsing argument ${argument_number} of command \"${command}\": ${detail}",
                                        ("argument_number", parameter_index + 1)("command", method_data.name)("detail", e.get_log()));
                }
              }
            } FC_RETHROW_EXCEPTIONS( warn, "", ("parameter_index",parameter_index) ) }

            fc::variant execute_interactive_command(const string& command, const fc::variants& arguments)
            {
              if(command == "wallet_rescan_blockchain")
              {
                  if ( ! _client->get_wallet()->is_open() )
                      interactive_open_wallet();
                  if( ! _client->get_wallet()->is_unlocked() )
                  {
                    // unlock wallet for 5 minutes
                    fc::istream_ptr argument_stream = std::make_shared<fc::stringstream>("300");
                    try
                    {
                      parse_and_execute_interactive_command( "wallet_unlock", argument_stream );
                    }
                    catch( const fc::canceled_exception& )
                    {
                    }
                  } 

                  *_out << "Rescanning blockchain...\n";
                  uint32_t start;
                  if (arguments.size() == 0)
                      start = 0;
                  else
                      start = arguments[0].as<uint32_t>();
                  while(true)
                  {
                      try {
                        if (!_filter_output_for_tests)
                        {
                          *_out << "|";
                          for(int i = 0; i < 100; i++)
                              *_out << "-";
                          *_out << "|\n|=";
                          uint32_t next_step = 0;
                          //TODO: improve documentation for how this works
                          auto cb = [=](uint32_t cur, uint32_t last) mutable
                          {
                              if (start > last || cur >= last) // if WTF
                                  return;
                              if (((100*(1 + cur - start)) / (1 + last - start)) > next_step)
                              {
                                  *_out << "=";
                                  next_step++;
                              }
                          };
                          _client->get_wallet()->scan_chain(start, -1, cb);
                          *_out << "|\n";
                        }
                        else
                        {
                          _client->get_wallet()->scan_chain(start, -1);
                        }
                        *_out << "Scan complete.\n";
                        return fc::variant("Scan complete.");
                      }
                      catch( const rpc_wallet_open_needed_exception& )
                      {
                          interactive_open_wallet();
                      }                
                      catch( const rpc_wallet_unlock_needed_exception& )
                      {
                        // unlock wallet for 5 minutes
                        fc::istream_ptr argument_stream = std::make_shared<fc::stringstream>("300");
                        try
                        {
                          parse_and_execute_interactive_command( "wallet_unlock", argument_stream );
                        }
                        catch( const fc::canceled_exception& )
                        {
                        }
                      }
 
                  }
              }
              else if(command == "quit" || command == "stop" || command == "exit")
              {
                _quit = true;
                FC_THROW_EXCEPTION(fc::canceled_exception, "quit command issued");
              }
              
              return execute_command(command, arguments);
            }

            fc::variant execute_command(const string& command, const fc::variants& arguments)
            { try {
              while (true)
              {
                // try to execute the method.  If the method needs the wallet to be
                // unlocked, it will throw an exception, and we'll attempt to
                // unlock it and then retry the command
                try
                {
                    return _rpc_server->direct_invoke_method(command, arguments);
                }
                catch( const rpc_wallet_open_needed_exception& )
                {
                    interactive_open_wallet();
                }
                catch( const rpc_wallet_unlock_needed_exception& )
                {
                    fc::istream_ptr unlock_time_arguments = std::make_shared<fc::stringstream>("300"); // default to five minute timeout
                    parse_and_execute_interactive_command( "wallet_unlock", unlock_time_arguments );
                }
              }
            } FC_RETHROW_EXCEPTIONS( warn, "", ("command",command) ) }

            string prompt_for_input( const string& prompt, bool no_echo = false, bool verify = false )
            { try {
                string input;
                while( true )
                {
                    input = get_line( prompt + ": ", no_echo );
                    if( input.empty() ) FC_THROW_EXCEPTION(fc::canceled_exception, "input aborted");

                    if( verify )
                    {
                        if( input != get_line( prompt + " (verify): ", no_echo ) )
                        {
                            *_out << "Input did not match, try again\n";
                            continue;
                        }
                    }
                    break;
                }
                return input;
            } FC_CAPTURE_AND_RETHROW( (prompt)(no_echo) ) }

            void interactive_open_wallet()
            {
              if( _client->get_wallet()->is_open() ) 
                return;

              *_out << "A wallet must be open to execute this command. You can:\n";
              *_out << "(o) Open an existing wallet\n";
              *_out << "(c) Create a new wallet\n";
              *_out << "(q) Abort command\n";

              string choice = get_line("Choose [o/c/q]: ");

              if (choice == "c")
              {
                fc::istream_ptr argument_stream = std::make_shared<fc::stringstream>();
                try
                {
                  parse_and_execute_interactive_command( "wallet_create", argument_stream );
                }
                catch( const fc::canceled_exception& )
                {
                }
              }
              else if (choice == "o")
              {
                fc::istream_ptr argument_stream = std::make_shared<fc::stringstream>();
                try
                {
                  parse_and_execute_interactive_command( "wallet_open", argument_stream );
                }
                catch( const fc::canceled_exception& )
                {
                }
              }
              else if (choice == "q")
              {
                FC_THROW_EXCEPTION(fc::canceled_exception, "");
              }
              else
              {
                  *_out << "Wrong answer!\n";
              }
            }

            void format_and_print_result(const string& command,
                                         const fc::variants arguments,
                                         const fc::variant& result)
            {
              string method_name = command;
              try
              {
                // command could be alias, so get the real name of the method.
                auto method_data = _rpc_server->get_method_data(command);
                method_name = method_data.name;
              }
              catch( const fc::key_not_found_exception& )
              {
                 elog( " KEY NOT FOUND " );
              }
              catch( ... )
              {
                 elog( " unexpected exception " );
              }

              if (show_raw_output)
              {
                  string result_type;
                  const bts::api::method_data& method_data = _rpc_server->get_method_data(method_name);
                  result_type = method_data.return_type;

                  if (result_type == "asset")
                  {
                    *_out << (string)result.as<bts::blockchain::asset>() << "\n";
                  }
                  else if (result_type == "address")
                  {
                    *_out << (string)result.as<bts::blockchain::address>() << "\n";
                  }
                  else if (result_type == "null" || result_type == "void")
                  {
                    *_out << "OK\n";
                  }
                  else
                  {
                    *_out << fc::json::to_pretty_string(result) << "\n";
                  }
              }
             
              if (method_name == "help")
              {
                string help_string = result.as<string>();
                *_out << help_string << "\n";
              }
              else if( method_name == "wallet_account_vote_summary" )
              {
                  if( !_out ) return;

                     (*_out) << std::setw(25) << std::left << "Delegate"
                            << std::setw(15) << "Votes\n";
                     (*_out) <<"--------------------------------------------------------------\n";
                  auto votes = result.as<bts::wallet::wallet::account_vote_summary_type>();
                  for( auto vote : votes )
                  {
                     (*_out) << std::setw(25) << vote.first 
                            << std::setw(15) << vote.second.votes_for <<"\n";
                  }
              }
              else if (method_name == "list_errors")
              {
                  auto error_map = result.as<map<fc::time_point,fc::exception> >();
                  if (error_map.empty())
                      *_out << "No errors.\n";
                  else
                    for( auto item : error_map )
                    {
                       (*_out) << string(item.first) << " (" << fc::get_approximate_relative_time_string( item.first ) << " )\n";
                       (*_out) << item.second.to_detail_string();
                       (*_out) << "\n";
                    }
              }
              else if (method_name == "wallet_account_transaction_history")
              {
                  auto tx_history_summary = result.as<vector<pretty_transaction>>();
                  print_transaction_history(tx_history_summary);
              }
              else if( method_name == "wallet_market_order_list" )
              {
                  auto order_list = result.as<vector<market_order_status> >();
                  print_wallet_market_order_list( order_list );
              }
              else if ( command == "wallet_list_my_accounts" )
              {
                  auto accts = result.as<vector<wallet_account_record>>();
                  print_receive_account_list( accts );
              }
              else if ( command == "wallet_list_accounts" || command == "wallet_list_unregistered_accounts" || command == "wallet_list_favorite_accounts" )
              {
                  auto accts = result.as<vector<wallet_account_record>>();
                  print_contact_account_list( accts );
              }
              else if (method_name == "wallet_account_balance" )
              {
                  const auto& summary = result.as<map<string, map<string, share_type>>>();
                  if( !summary.empty() )
                  {
                      auto bc = _client->get_chain();
                      for( const auto& accts : summary )
                      {
                          *_out << accts.first << ":\n";
                          for( const auto& balance : accts.second )
                          {
                             *_out << "    "
                                   << bc->to_pretty_asset( asset( balance.second, bc->get_asset_id( balance.first) ) )
                                   << "\n";
                          }
                      }
                  }
                  else
                  {
                      *_out << "No funds available.\n";
                  }
              }
              else if (method_name == "wallet_transfer")
              {
                  auto trx = result.as<signed_transaction>();
                  print_transfer_summary( trx );
              }
              else if (method_name == "wallet_list")
              {
                  auto wallets = result.as<vector<string>>();
                  if (wallets.empty())
                      *_out << "No wallets found.\n";
                  else
                      for (auto wallet : wallets)
                          *_out << wallet << "\n";
              }
              else if (method_name == "wallet_list_unspent_balances" )
              {
                  auto balance_recs = result.as<vector<wallet_balance_record>>();
                  print_unspent_balances(balance_recs);
              }
              else if (method_name == "network_get_usage_stats" )
              {
                  print_network_usage_stats(result.get_object());
              }
              else if (method_name == "blockchain_list_blocks")
              {
                  auto blocks = result.as<vector<bts::blockchain::block_record>>();

                  *_out << std::left;
                  *_out << std::setw(8) << "HEIGHT";
                  *_out << std::setw(20) << "TIMESTAMP";
                  *_out << std::setw(32) << "SIGNING DELEGATE";
                  *_out << std::setw(8) << "# TXS";
                  *_out << std::setw(8)  << "SIZE";
                  *_out << std::setw(16) << "TOTAL FEES";
                  *_out << std::setw(8)  << "LATENCY";
                  *_out << std::setw(15)  << "PROCESSING TIME";

                  *_out << '\n';
                  for (int i = 0; i < 115; ++i)
                      *_out << '-';
                  *_out << '\n';

                  for (const auto& block : blocks)
                  {
                      *_out << std::setw(8) << block.block_num
                            << std::setw(20) << time_to_string(block.timestamp)
                            << std::setw(32) << _client->blockchain_get_signing_delegate(block.block_num)
                            << std::setw(8) << block.user_transaction_ids.size()
                            << std::setw(8) << block.block_size
                            << std::setw(16) << block.total_fees / double( BTS_BLOCKCHAIN_PRECISION )
                            << std::setw(8) << block.latency
                            << std::setw(15) << block.processing_time.count() / double( 1000000 )
                            << '\n';
                  }
              }
              else if (method_name == "blockchain_list_registered_accounts")
              {
                  string start = "";
                  int32_t count = 25; // In CLI this is a more sane default
                  if (arguments.size() > 0)
                      start = arguments[0].as_string();
                  if (arguments.size() > 1)
                      count = arguments[1].as<int32_t>();
                  print_registered_account_list( result.as<vector<account_record>>(), count );
              }
              else if (method_name == "blockchain_list_registered_assets")
              {
                  auto records = result.as<vector<asset_record>>();
                  *_out << std::setw(6) << "ID";
                  *_out << std::setw(7) << "SYMBOL";
                  *_out << std::setw(15) << "NAME";
                  *_out << std::setw(35) << "DESCRIPTION";
                  *_out << std::setw(16) << "CURRENT_SUPPLY";
                  *_out << std::setw(16) << "MAX_SUPPLY";
                  *_out << std::setw(16) << "FEES COLLECTED";
                  *_out << std::setw(16) << "REGISTERED";
                  *_out << "\n";
                  for (auto asset : records)
                  {
                      *_out << std::setprecision(15);
                      *_out << std::setw(6) << asset.id;
                      *_out << std::setw(7) << asset.symbol;
                      *_out << std::setw(15) << pretty_shorten(asset.name, 14);
                      *_out << std::setw(35) << pretty_shorten(asset.description, 33);
                      *_out << std::setw(16) << double(asset.current_share_supply) / asset.precision;
                      *_out << std::setw(16) << double(asset.maximum_share_supply) / asset.precision;
                      *_out << std::setw(16) << double(asset.collected_fees) / asset.precision;
                      *_out << std::setw(16) << time_to_string(asset.registration_date);
                      *_out << "\n";
                  }
              }
              else if (method_name == "blockchain_list_delegates")
              {
                  auto delegates = result.as<vector<account_record>>();
                  uint32_t current = 0;
                  uint32_t count = 1000000;
                  if (arguments.size() > 0)
                      current = arguments[0].as<uint32_t>();
                  if (arguments.size() > 1)
                      count = arguments[1].as<uint32_t>();
                  auto max = current + count;
                  auto num_active = BTS_BLOCKCHAIN_NUM_DELEGATES - current;

                  *_out << std::setw(5) << "ID";
                  *_out << std::setw(30) << "NAME";
                  *_out << std::setw(20) << "APPROVAL";
                  *_out << std::setw(16) << "BLOCKS PRODUCED";
                  *_out << std::setw(16) << "BLOCKS MISSED";
                  *_out << "\n---------------------------------------------------------\n";

                  if (current < num_active)
                      *_out << "** Top:\n";

                  auto total_delegates = delegates.size();
                  for( auto delegate_rec : delegates )
                  {
                      if (current > max || current == total_delegates)
                          return;
                      if (current == num_active)
                          *_out << "** Standby:\n";

                      *_out << std::setw(5)  << std::left << delegate_rec.id;
                      *_out << std::setw(30) << pretty_shorten( delegate_rec.name, 24 );
                      
                      std::stringstream ss;
                      auto opt_rec = _client->get_chain()->get_asset_record(asset_id_type(0));
                      FC_ASSERT(opt_rec.valid(), "No asset with id 0??");
                      float percent = 100.f * delegate_rec.net_votes() / opt_rec->current_share_supply;
                      ss << std::setprecision(10);
                      ss << std::fixed;
                      ss << percent;
                      ss << " %";
                      //*_out << std::setw(20) << _client->get_chain()->to_pretty_asset( asset(delegate_rec.net_votes(),0) );
                      *_out << std::setw(20) << ss.str();

                      *_out << std::setw(16) << delegate_rec.delegate_info->blocks_produced;
                      *_out << std::setw(16) << delegate_rec.delegate_info->blocks_missed;

                      *_out << "\n";
                      current++;
                  }
                  *_out << "  Use \"blockchain_list_delegates <start_num> <count>\" to see more.\n";
              }
              else if (method_name == "blockchain_get_proposal_votes")
              {
                  auto votes = result.as<vector<proposal_vote>>();
                  *_out << std::left;
                  *_out << std::setw(15) << "DELEGATE";
                  *_out << std::setw(22) << "TIME";
                  *_out << std::setw(5)  << "VOTE";
                  *_out << std::setw(35) << "MESSAGE";
                  *_out << "\n----------------------------------------------------------------";
                  *_out << "-----------------------\n";
                  for (auto vote : votes)
                  {
                      auto rec = _client->get_chain()->get_account_record( vote.id.delegate_id );
                      *_out << std::setw(15) << pretty_shorten(rec->name, 14);
                      *_out << std::setw(20) << time_to_string(vote.timestamp);
                      if (vote.vote == proposal_vote::no)
                      {
                          *_out << std::setw(5) << "NO";
                      }
                      else if (vote.vote == proposal_vote::yes)
                      {
                          *_out << std::setw(5) << "YES";
                      }
                      else
                      {
                          *_out << std::setw(5) << "??";
                      }
                      *_out << std::setw(35) << pretty_shorten(vote.message, 35);
                      *_out << "\n";
                  }
                  *_out << "\n";
              }
              else if (method_name.find("blockchain_get_account_record") != std::string::npos)
              {
                  // Pretty print of blockchain_get_account_record{,_by_id}
                  bts::blockchain::account_record record;
                  try
                  {
                      record = result.as<bts::blockchain::account_record>();
                  }
                  catch (...)
                  {
                      *_out << "No record found.\n";
                      return;
                  }
                  *_out << "Record for '" << record.name << "' -- Registered on ";
                  *_out << boost::posix_time::to_simple_string(
                             boost::posix_time::from_time_t(time_t(record.registration_date.sec_since_epoch())));
                  *_out << ", last update was " << fc::get_approximate_relative_time_string(record.last_update) << "\n";
                  *_out << "Owner's key: " << std::string(record.owner_key) << "\n";

                  //Only print active key history if there are keys in the history which are not the owner's key above.
                  if (record.active_key_history.size() > 1 || record.active_key_history.begin()->second != record.owner_key)
                  {
                      *_out << "History of active keys:\n";

                      for (auto key : record.active_key_history)
                          *_out << "  Key " << std::string(key.second) << " last used " << fc::get_approximate_relative_time_string(key.first) << "\n";
                  }

                  if (record.is_delegate())
                  {
                      *_out << std::left;
                      *_out << std::setw(20) << "NET VOTES";
                      *_out << std::setw(16) << "BLOCKS PRODUCED";
                      *_out << std::setw(16) << "BLOCKS MISSED";
                      *_out << std::setw(20) << "PRODUCTION RATIO";
                      *_out << std::setw(16) << "LAST BLOCK #";
                      *_out << std::setw(20) << "TOTAL PAY";
                      _out->put('\n');
                      for (int i=0; i < 148; ++i)
                          _out->put('-');
                      _out->put('\n');

                      auto supply = _client->get_chain()->get_asset_record(bts::blockchain::asset_id_type(0))->current_share_supply;
                      auto last_block_produced = record.delegate_info->last_block_num_produced;
                                                //Horribly painful way to print a % after a double with precision of 8. Better solutions welcome.
                      *_out << std::setw(20) << (fc::variant(double(record.net_votes())*100.0 / supply).as_string().substr(0,10) + '%')
                            << std::setw(16) << record.delegate_info->blocks_produced
                            << std::setw(16) << record.delegate_info->blocks_missed
                            << std::setw(20) << std::setprecision(4) << (double(record.delegate_info->blocks_produced) / (record.delegate_info->blocks_produced + record.delegate_info->blocks_missed));

                            if( last_block_produced != uint32_t( -1 ) )
                                *_out << std::setw(16) << last_block_produced;
                            else
                                *_out << std::setw(16) << "N/A";

                      *_out << _client->get_chain()->to_pretty_asset(asset(record.delegate_pay_balance()))
                            << "\n";
                  }
                  else
                  {
                      *_out << "This account is not a delegate.\n";
                  }

                  if(!record.public_data.is_null())
                      *_out << "Public data:\n" << fc::json::to_pretty_string(record.public_data) << "\n";
              }
              else if (method_name == "blockchain_list_forks")
              {
                  std::map<uint32_t, std::vector<fork_record>> forks = result.as<std::map<uint32_t, std::vector<fork_record>>>();
                  std::map<block_id_type, std::string> invalid_reasons; //Your reasons are invalid.

                  if (forks.empty())
                      *_out << "No forks.\n";
                  else
                  {
                      *_out << std::setw(15) << "FORKED BLOCK"
                            << std::setw(30) << "FORKING BLOCK ID"
                            << std::setw(30) << "SIGNING DELEGATE"
                            << std::setw(15) << "TXN COUNT"
                            << std::setw(10) << "SIZE"
                            << std::setw(20) << "TIMESTAMP"
                            << std::setw(10) << "LATENCY"
                            << std::setw(8)  << "VALID"
                            << std::setw(20)  << "IN CURRENT CHAIN"
                            << "\n" << std::string(158, '-') << "\n";

                      for (auto fork : forks)
                      {
                          *_out << std::setw(15) << fork.first << "\n";

                          for (auto tine : fork.second)
                          {
                              *_out << std::setw(45) << fc::variant(tine.block_id).as_string();

                              auto delegate_record = _client->get_chain()->get_account_record(tine.signing_delegate);
                              if (delegate_record.valid() && delegate_record->name.size() < 29)
                                  *_out << std::setw(30) << delegate_record->name;
                              else
                                  *_out << std::setw(30) << std::string("Delegate ID ") + fc::variant(tine.signing_delegate).as_string();

                              *_out << std::setw(15) << tine.transaction_count
                                    << std::setw(10) << tine.size
                                    << std::setw(20) << time_to_string(tine.timestamp)
                                    << std::setw(10) << tine.latency
                                    << std::setw(8);

                              if (tine.is_valid.valid()) {
                                  if (*tine.is_valid) {
                                      *_out << "YES";
                                  }
                                  else {
                                      *_out << "NO";
                                      if (tine.invalid_reason.valid())
                                          invalid_reasons[tine.block_id] = tine.invalid_reason->to_detail_string();
                                      else
                                          invalid_reasons[tine.block_id] = "No reason given.";
                                  }
                              }
                              else
                                  *_out << "N/A";

                              *_out << std::setw(20);
                              if (tine.is_current_fork)
                                  *_out << "YES";
                              else
                                      *_out << "NO";

                              *_out << "\n";
                          }
                      }

                      if (invalid_reasons.size() > 0) {
                          *_out << "REASONS FOR INVALID BLOCKS\n";

                          for (auto excuse : invalid_reasons)
                              *_out << excuse.first << ": " << excuse.second << "\n";
                      }
                  }
              }
              else if (method_name == "blockchain_get_pending_transactions")
              {
                  auto transactions = result.as<vector<signed_transaction>>();

                  if(transactions.empty())
                  {
                      *_out << "No pending transactions.\n";
                  }
                  else
                  {
                      *_out << std::setw(10) << "TXN ID"
                            << std::setw(10) << "SIZE"
                            << std::setw(25) << "OPERATION COUNT"
                            << std::setw(25) << "SIGNATURE COUNT"
                            << "\n" << std::string(70, '-') << "\n";

                      for (signed_transaction transaction : transactions)
                      {
                          *_out << std::setw(10) << transaction.id().str().substr(0, 8)
                                << std::setw(10) << transaction.data_size()
                                << std::setw(25) << transaction.operations.size()
                                << std::setw(25) << transaction.signatures.size()
                                << "\n";
                      }
                  }
              }
              else if (method_name == "blockchain_list_current_round_active_delegates")
              {
                  map<account_id_type, string> delegates = result.as<map<account_id_type, string>>();
                  unsigned longest_delegate_name = 0;

                  //Yeah, it's slower, I know, but it's so ugly if I don't. And seriously, it's still O(n) so stop complaining.
                  for (auto delegate : delegates)
                    if (delegate.second.size() > longest_delegate_name)
                        longest_delegate_name = delegate.second.size();

                  unsigned name_column_width = 20;
                  if (longest_delegate_name + 3 > name_column_width)
                      name_column_width = longest_delegate_name + 3;

                  *_out << std::setw(name_column_width) << "DELEGATE NAME"
                        << std::setw(5) << "ID"
                        << "\n" << std::string(name_column_width + 5, '-') << "\n";

                  for (auto delegate : delegates)
                  {
                      *_out << std::setw(name_column_width) << delegate.second
                            << std::setw(5) << delegate.first
                            << "\n";
                  }
              }
              else if (method_name == "blockchain_list_proposals")
              {
                  auto proposals = result.as<vector<proposal_record>>();
                  *_out << std::left;
                  *_out << std::setw(10) << "ID";
                  *_out << std::setw(20) << "SUBMITTED BY";
                  *_out << std::setw(22) << "SUBMIT TIME";
                  *_out << std::setw(15) << "TYPE";
                  *_out << std::setw(20) << "SUBJECT";
                  *_out << std::setw(35) << "BODY";
                  *_out << std::setw(20) << "DATA";
                  *_out << std::setw(10)  << "RATIFIED";
                  *_out << "\n------------------------------------------------------------";
                  *_out << "-----------------------------------------------------------------";
                  *_out << "------------------\n";
                  for (auto prop : proposals)
                  {
                      *_out << std::setw(10) << prop.id;
                      auto delegate_rec = _client->get_chain()->get_account_record(prop.submitting_delegate_id);
                      *_out << std::setw(20) << pretty_shorten(delegate_rec->name, 19);
                      *_out << std::setw(20) << time_to_string(prop.submission_date);
                      *_out << std::setw(15) << pretty_shorten(prop.proposal_type, 14);
                      *_out << std::setw(20) << pretty_shorten(prop.subject, 19);
                      *_out << std::setw(35) << pretty_shorten(prop.body, 34);
                      *_out << std::setw(20) << pretty_shorten(fc::json::to_pretty_string(prop.data), 19);
                      *_out << std::setw(10) << (prop.ratified ? "YES" : "NO");
                  }
                  *_out << "\n"; 
              }
              else if (method_name == "network_list_potential_peers")
              {
                  auto peers = result.as<std::vector<net::potential_peer_record>>();
                  *_out << std::setw(25) << "ENDPOINT";
                  *_out << std::setw(25) << "LAST SEEN";
                  *_out << std::setw(25) << "LAST CONNECT ATTEMPT";
                  *_out << std::setw(30) << "SUCCESSFUL CONNECT ATTEMPTS";
                  *_out << std::setw(30) << "FAILED CONNECT ATTEMPTS";
                  *_out << std::setw(35) << "LAST CONNECTION DISPOSITION";
                  *_out << std::setw(30) << "LAST ERROR";

                  *_out<< "\n";
                  for (auto peer : peers)
                  {
                      *_out<< std::setw(25) << string(peer.endpoint);
                      *_out<< std::setw(25) << time_to_string(peer.last_seen_time);
                      *_out << std::setw(25) << time_to_string(peer.last_connection_attempt_time);
                      *_out << std::setw(30) << peer.number_of_successful_connection_attempts;
                      *_out << std::setw(30) << peer.number_of_failed_connection_attempts;
                      *_out << std::setw(35) << string( peer.last_connection_disposition );
                      *_out << std::setw(30) << (peer.last_error ? peer.last_error->to_detail_string() : "none");

                      *_out << "\n";
                  }
              }
              else
              {
                // there was no custom handler for this particular command, see if the return type
                // is one we know how to pretty-print
                string result_type;
                try
                {
                  const bts::api::method_data& method_data = _rpc_server->get_method_data(method_name);
                  result_type = method_data.return_type;

                  if (result_type == "asset")
                  {
                    *_out << (string)result.as<bts::blockchain::asset>() << "\n";
                  }
                  else if (result_type == "address")
                  {
                    *_out << (string)result.as<bts::blockchain::address>() << "\n";
                  }
                  else if (result_type == "null" || result_type == "void")
                  {
                    *_out << "OK\n";
                  }
                  else
                  {
                    *_out << fc::json::to_pretty_string(result) << "\n";
                  }
                }
                catch( const fc::key_not_found_exception& )
                {
                   elog( " KEY NOT FOUND " );
                   *_out << "key not found \n";
                }
                catch( ... )
                {
                   *_out << "unexpected exception \n";
                }
              }

              *_out << std::right; /* Ensure default alignment is restored */
            }

            string pretty_shorten(const string& str, uint32_t size)
            {
                if (str.size() < size)
                    return str;
                return str.substr(0, size - 3) + "...";
            }

            void print_transfer_summary(const signed_transaction& trx)
            {
                auto trx_rec = _client->get_wallet()->lookup_transaction(trx.id());
                auto pretty = _client->get_wallet()->to_pretty_trx( *trx_rec );
                std::vector<pretty_transaction> list = { pretty };
                print_transaction_history(list);
            }

            void print_unspent_balances(const vector<wallet_balance_record>& balance_recs)
            {
                *_out << std::right;
                *_out << std::setw(18) << "BALANCE";
                *_out << std::right << std::setw(40) << "OWNER";
                *_out << std::right << std::setw(25) << "VOTE";
                *_out << "\n";
                *_out << "-------------------------------------------------------------";
                *_out << "-------------------------------------------------------------\n";
                for( auto balance_rec : balance_recs )
                {
                    *_out << std::setw(18) << _client->get_chain()->to_pretty_asset( asset(balance_rec.balance, 0) );
                    switch (withdraw_condition_types(balance_rec.condition.type))
                    {
                        case (withdraw_signature_type):
                        {
                            auto cond = balance_rec.condition.as<withdraw_with_signature>();
                            auto acct_rec = _client->get_wallet()->get_account_record( cond.owner );
                            string owner;
                            if ( acct_rec.valid() )
                                owner = acct_rec->name;
                            else
                                owner = string( balance_rec.owner() );

                            if (owner.size() > 36)
                            {
                                *_out << std::setw(40) << owner.substr(0, 31) << "...";
                            }
                            else
                            {
                                *_out << std::setw(40) << owner;
                            }

                            /*
                             * TODO... what about the slate??
                            auto delegate_id = balance_rec.condition.delegate_id;
                            auto delegate_rec = _client->get_chain()->get_account_record( delegate_id );
                            if( delegate_rec )
                            {
                               string sign = (delegate_id > 0 ? "+" : "-");
                               if (delegate_rec->name.size() > 21)
                               {
                                   *_out << std::setw(25) << (sign + delegate_rec->name.substr(0, 21) + "...");
                               }
                               else
                               {
                                   *_out << std::setw(25) << (sign + delegate_rec->name);
                               }
                               break;
                            }
                            else
                            {
                                   *_out << std::setw(25) << "none";
                            }
                            */
                        }
                        default:
                        {
                            FC_ASSERT(!"unimplemented condition type");
                        }
                    } // switch cond type
                    *_out << "\n";
                } // for balance in balances
            }

            void print_contact_account_list(const vector<wallet_account_record> account_records)
            {
                *_out << std::setw( 35 ) << std::left << "NAME (* delegate)";
                *_out << std::setw( 64 ) << "KEY";
                *_out << std::setw( 22 ) << "REGISTERED";
                *_out << std::setw( 15 ) << "FAVORITE";
                *_out << std::setw( 15 ) << "TRUSTED";
                *_out << "\n";

                for( auto acct : account_records )
                {
                    if (acct.is_delegate())
                      *_out << std::setw(35) << pretty_shorten(acct.name, 33) + " *";
                    else
                      *_out << std::setw(35) << pretty_shorten(acct.name, 34);

                    *_out << std::setw(64) << string( acct.active_key() );

                    if( acct.id == 0 )
                      *_out << std::setw( 22 ) << "NO";
                    else 
                      *_out << std::setw( 22 ) << time_to_string(acct.registration_date);

                    if( acct.is_favorite )
                      *_out << std::setw( 15 ) << "YES";
                    else
                      *_out << std::setw( 15 ) << "NO";

                    *_out << std::setw( 10) << acct.trusted;
                    *_out << "\n";
                }
            }

            void print_receive_account_list(const vector<wallet_account_record>& account_records)
            {
                *_out << std::setw( 35 ) << std::left << "NAME (* delegate)";
                *_out << std::setw( 64 ) << "KEY";
                *_out << std::setw( 22 ) << "REGISTERED";
                *_out << std::setw( 15 ) << "FAVORITE";
                *_out << std::setw( 15 ) << "TRUSTED";
                *_out << std::setw( 25 ) << "BLOCK PRODUCTION ENABLED";
                *_out << "\n";

                for( auto acct : account_records )
                {
                    if (acct.is_delegate())
                    {
                        *_out << std::setw(35) << pretty_shorten(acct.name, 33) + " *";
                    }
                    else
                    {
                        *_out << std::setw(35) << pretty_shorten(acct.name, 34);
                    }

                    *_out << std::setw(64) << string( acct.active_key() );

                    if (acct.id == 0 ) 
                    {
                        *_out << std::setw( 22 ) << "NO";
                    } 
                    else 
                    {
                        *_out << std::setw( 22 ) << time_to_string(acct.registration_date);
                    }

                    if( acct.is_favorite )
                      *_out << std::setw( 15 ) << "YES";
                    else
                      *_out << std::setw( 15 ) << "NO";

                    *_out << std::setw( 15 ) << acct.trusted;
                    if (acct.is_delegate())
                        *_out << std::setw( 25 ) << (acct.block_production_enabled ? "YES" : "NO");
                    else
                        *_out << std::setw( 25 ) << "N/A";
                    *_out << "\n";
                }
            }

            void print_registered_account_list(const vector<account_record> account_records, int32_t count )
            {
                *_out << std::setw( 35 ) << std::left << "NAME (* delegate)";
                *_out << std::setw( 64 ) << "KEY";
                *_out << std::setw( 22 ) << "REGISTERED";
                *_out << std::setw( 15 ) << "VOTES FOR";
                *_out << std::setw( 15 ) << "TRUSTED";

                *_out << '\n';
                for (int i = 0; i < 151; ++i)
                    *_out << '-';
                *_out << '\n';

                auto counter = 0;
                for( auto acct : account_records )
                {
                    if (acct.is_delegate())
                    {
                        *_out << std::setw(35) << pretty_shorten(acct.name, 33) + " *";
                    }
                    else
                    {
                        *_out << std::setw(35) << pretty_shorten(acct.name, 34);
                    }
                    
                    *_out << std::setw(64) << string( acct.active_key() );
                    *_out << std::setw( 22 ) << time_to_string(acct.registration_date);

                    if ( acct.is_delegate() )
                    {
                        *_out << std::setw(15) << acct.delegate_info->votes_for;

                        try
                        {
                            auto trust = _client->get_wallet()->get_delegate_trust( acct.name );
                            *_out << std::setw( 15 ) << trust;
                        }
                        catch( ... )
                        {
                            *_out << std::setw(15) << "???";
                        }
                    }
                    else
                    {
                        *_out << std::setw(15) << "N/A";
                        *_out << std::setw(15) << "N/A";
                    }

                    *_out << "\n";

                    /* Count is positive b/c CLI overrides default -1 arg */
                    if (counter >= count)
                    {
                        *_out << "... Use \"blockchain_list_registered_accounts <start_name> <count>\" to see more.\n";
                        return;
                    }
                    counter++;

                }
            }
            void print_wallet_market_order_list( const vector<market_order_status>& order_list )
            {
                if( !_out ) return;

                std::ostream& out = *_out;

                out << std::setw( 40 ) << std::left << "ID";
                out << std::setw( 10 )  << "TYPE";
                out << std::setw( 20 ) << "QUANTITY";
                out << std::setw( 20 ) << "PRICE";
                out << std::setw( 20 ) << "COST";
                out << "\n";
                out <<"-----------------------------------------------------------------------------------------------------------\n";

                for( auto order : order_list )
                {
                   out << std::setw( 40 )  << std::left << variant( order.order.market_index.owner ).as_string(); //order.get_id();
                   out << std::setw( 10  )  << variant( order.get_type() ).as_string();
                   out << std::setw( 20  ) << _client->get_chain()->to_pretty_asset( order.get_quantity() );
                   out << std::setw( 20  ) << _client->get_chain()->to_pretty_price( order.get_price() ); //order.market_index.order_price );
                   out << std::setw( 20  ) << _client->get_chain()->to_pretty_asset( order.get_balance() );
                   out << "\n";
                }
                out << "\n";
            }

            void print_transaction_history(const vector<bts::wallet::pretty_transaction> txs)
            {
                /* Print header */
                if( !_out ) return;

                (*_out) << std::setw(  7 ) << "BLK" << ".";
                (*_out) << std::setw(  5 ) << std::left << "TRX";
                (*_out) << std::setw( 20 ) << "TIMESTAMP";
                (*_out) << std::setw( 20 ) << "FROM";
                (*_out) << std::setw( 20 ) << "TO";
                (*_out) << std::setw( 35 ) << "MEMO";
                (*_out) << std::setw( 20 ) << std::right << "AMOUNT";
                (*_out) << std::setw( 20 ) << "FEE    ";
                (*_out) << std::setw( 12 ) << "ID";
                (*_out) << "\n---------------------------------------------------------------------------------------------------";
                (*_out) <<   "-------------------------------------------------------------------------\n";
                (*_out) << std::right; 
                
                int count = 1;
                for( auto tx : txs )
                {
                    count++;

                    /* Print block and transaction numbers */
                    if (tx.block_num == -1)
                    {
                        (*_out) << std::setw( 13 ) << std::left << "   pending";
                    }
                    else
                    {
                        (*_out) << std::setw( 7 ) << tx.block_num << ".";
                        (*_out) << std::setw( 5 ) << std::left << tx.trx_num;
                    }

                    /* Print timestamp */
                     (*_out) << std::setw( 20 ) << time_to_string(fc::time_point_sec(tx.received_time));

                    // Print "from" account
                     (*_out) << std::setw( 20 ) << pretty_shorten(tx.from_account, 19);
                    
                    // Print "to" account
                     (*_out) << std::setw( 20 ) << pretty_shorten(tx.to_account, 19);

                    // Print "memo" on transaction
                     (*_out) << std::setw( 35 ) << pretty_shorten(tx.memo_message, 34);

                    /* Print amount */
                    {
                        (*_out) << std::right;
                        std::stringstream ss;
                        ss << _client->get_chain()->to_pretty_asset(tx.amount);
                        (*_out) << std::setw( 20 ) << ss.str();
                    }

                    /* Print fee */
                    {
                        (*_out) << std::right;
                        std::stringstream ss;
                        ss << _client->get_chain()->to_pretty_asset( asset(tx.fees,0 ));
                        (*_out) << std::setw( 20 ) << ss.str();
                    }

                    *_out << std::right;
                    /* Print delegate vote */
                    /*
                    bool has_deposit = false;
                    std::stringstream ss;
                    for (auto op : tx.operations)
                    {
                        if (op.get_object()["op_name"].as<string>() == "deposit")
                        {
                            has_deposit = true;
                            auto vote = op.as<pretty_deposit_op>().vote;
                            if( vote.first > 0 )
                                ss << "+";
                            else if (vote.first < 0)
                                ss << "-";
                            else
                                ss << " ";
                            ss << vote.second;
                            break;
                        }
                    }
                    if (has_deposit)
                    {
                        (*_out) << std::setw(14) << ss.str();
                    }
                    else
                    {
                    }
                    */

                    (*_out) << std::right;
                    /* Print transaction ID */
                    if (_filter_output_for_tests)
                      (*_out) << std::setw( 16 ) << "[trx_id redacted]";
                    else
                      (*_out) << std::setw( 16 ) << string(tx.trx_id).substr(0, 8);// << "  " << string(tx.trx_id);

                    (*_out) << std::right << "\n";
                }
            }
            void print_network_usage_graph(const std::vector<uint32_t>& usage_data)
            {
              uint32_t max_value = *boost::max_element(usage_data);
              uint32_t min_value = *boost::min_element(usage_data);
              const unsigned num_rows = 10;
              for (unsigned row = 0; row < num_rows; ++row)
              {
                uint32_t threshold = min_value + (max_value - min_value) / (num_rows - 1) * (num_rows - row - 1);
                for (unsigned column = 0; column < usage_data.size(); ++column)
                  (*_out) << (usage_data[column] >= threshold ?  "*" : " ");
                (*_out) << " " << threshold << " bytes/sec\n";
              }
              (*_out)  << "\n";
            }
            void print_network_usage_stats(const fc::variant_object stats)
            {
              std::vector<uint32_t> usage_by_second = stats["usage_by_second"].as<std::vector<uint32_t> >();
              if (!usage_by_second.empty())
              {
                (*_out) << "last minute:\n";
                print_network_usage_graph(usage_by_second);
                (*_out)  << "\n";
              }
              std::vector<uint32_t> usage_by_minute = stats["usage_by_minute"].as<std::vector<uint32_t> >();
              if (!usage_by_minute.empty())
              {
                (*_out) << "last hour:\n";
                print_network_usage_graph(usage_by_minute);
                (*_out)  << "\n";
              }
              std::vector<uint32_t> usage_by_hour = stats["usage_by_hour"].as<std::vector<uint32_t> >();
              if (!usage_by_hour.empty())
              {
                (*_out) << "by hour:\n";
                print_network_usage_graph(usage_by_hour);
                (*_out)  << "\n";
              }
            }

            std::string time_to_string(const fc::time_point_sec& time);

            void display_status_message(const std::string& message);
#ifdef HAVE_READLINE
            typedef std::map<string, bts::api::method_data> method_data_map_type;
            method_data_map_type _method_data_map;
            typedef std::map<string, string>  method_alias_map_type;
            method_alias_map_type _method_alias_map;
            method_alias_map_type::iterator _command_completion_generator_iter;
            bool _method_data_is_initialized;
            void initialize_method_data_if_necessary();
            char* json_command_completion_generator(const char* text, int state);
            char* json_argument_completion_generator(const char* text, int state);
            char** json_completion(const char* text, int start, int end);
#endif
      };

#ifdef HAVE_READLINE
    static cli_impl* cli_impl_instance = NULL;
    extern "C" char** json_completion_function(const char* text, int start, int end);
    extern "C" int control_c_handler(int count, int key);
    extern "C" int get_character(FILE* stream);
#endif

    cli_impl::cli_impl(bts::client::client* client, std::istream* command_script, std::ostream* output_stream)
    :_client(client)
    ,_rpc_server(client->get_rpc_server())
    ,_quit(false)
    ,show_raw_output(false)
    ,_daemon_mode(false)
    ,nullstream(boost::iostreams::null_sink())
    , _saved_out(nullptr)
    ,_out(output_stream ? output_stream : &nullstream)
    ,_command_script(command_script)
    ,_filter_output_for_tests(false)
    {
#ifdef HAVE_READLINE
      //if( &output_stream == &std::cout ) // readline
      {
         cli_impl_instance = this;
         _method_data_is_initialized = false;
         rl_attempted_completion_function = &json_completion_function;
         rl_getc_function = &get_character;
      }
#ifndef __APPLE__
      // TODO: find out why this isn't defined on APPL
      //rl_bind_keyseq("\\C-c", &control_c_handler);
#endif
#endif
    }

#ifdef HAVE_READLINE
    void cli_impl::initialize_method_data_if_necessary()
    {
      if (!_method_data_is_initialized)
      {
        _method_data_is_initialized = true;
        vector<bts::api::method_data> method_data_list = _rpc_server->get_all_method_data();
        for (const bts::api::method_data& method_data : method_data_list)
        {
          _method_data_map[method_data.name] = method_data;
          _method_alias_map[method_data.name] = method_data.name;
          for (const string& alias : method_data.aliases)
            _method_alias_map[alias] = method_data.name;
        }
      }
    }
    extern "C" int get_character(FILE* stream)
    {
      return cli_impl_instance->_cin_thread.async([stream](){ return rl_getc(stream); }).wait();
    }
    extern "C" char* json_command_completion_generator_function(const char* text, int state)
    {
      return cli_impl_instance->json_command_completion_generator(text, state);
    }
    extern "C" char* json_argument_completion_generator_function(const char* text, int state)
    {
      return cli_impl_instance->json_argument_completion_generator(text, state);
    }
    extern "C" char** json_completion_function(const char* text, int start, int end)
    {
      return cli_impl_instance->json_completion(text, start, end);
    }
    extern "C" int control_c_handler(int count, int key)
    {
       std::cout << "\n\ncontrol-c!\n\n";
      return 0;
    }

    // implement json command completion (for function names only)
    char* cli_impl::json_command_completion_generator(const char* text, int state)
    {
      initialize_method_data_if_necessary();

      if (state == 0)
        _command_completion_generator_iter = _method_alias_map.lower_bound(text);
      else
        ++_command_completion_generator_iter;

      while (_command_completion_generator_iter != _method_alias_map.end())
      {
        if (_command_completion_generator_iter->first.compare(0, strlen(text), text) != 0)
          break; // no more matches starting with this prefix

        if (_command_completion_generator_iter->second == _command_completion_generator_iter->first) // suppress completing aliases
          return strdup(_command_completion_generator_iter->second.c_str());
        else
          ++_command_completion_generator_iter;  
      }

      rl_attempted_completion_over = 1; // suppress default filename completion
      return 0;
    }
    char* cli_impl::json_argument_completion_generator(const char* text, int state)
    {
      rl_attempted_completion_over = 1; // suppress default filename completion
      return 0;
    }
    char** cli_impl::json_completion(const char* text, int start, int end)
    {
      if (start == 0) // beginning of line, match a command
        return rl_completion_matches(text, &json_command_completion_generator_function);
      else
      {
        // not the beginning of a line.  figure out what the type of this argument is 
        // and whether we can complete it.  First, look up the method
        string command_line_to_parse(rl_line_buffer, start);
        string trimmed_command_to_parse(boost::algorithm::trim_copy(command_line_to_parse));
        
        if (!trimmed_command_to_parse.empty())
        {
          auto alias_iter = _method_alias_map.find(trimmed_command_to_parse);
          if (alias_iter != _method_alias_map.end())
          {
            auto method_data_iter = _method_data_map.find(alias_iter->second);
            if (method_data_iter != _method_data_map.end())
            {
            }
          }
          try
          {
            const bts::api::method_data& method_data = cli_impl_instance->_rpc_server->get_method_data(trimmed_command_to_parse);
            if (method_data.name == "help")
            {
                return rl_completion_matches(text, &json_command_completion_generator_function);
            }
          }
          catch( const bts::rpc::unknown_method& )
          {
            // do nothing
          }
        }
        
        return rl_completion_matches(text, &json_argument_completion_generator_function);
      }
    }

#endif
    void cli_impl::display_status_message(const std::string& message)
    {
      if( !_input_stream || !_out || _daemon_mode ) 
        return;
#ifdef HAVE_READLINE
      if (rl_prompt)
      {
        char* saved_line = rl_copy_text(0, rl_end);
        char* saved_prompt = strdup(rl_prompt);
        int saved_point = rl_point;
        rl_set_prompt("");
        rl_replace_line("", 0);
        rl_redisplay();
        (*_out) << message << "\n";
        _out->flush();
        rl_set_prompt(saved_prompt);
        rl_replace_line(saved_line, 0);
        rl_point = saved_point;
        rl_redisplay();
        free(saved_line);
        free(saved_prompt);
      }
      else
      {
        // it's not clear what state we're in if rl_prompt is null, but we've had reports
        // of crashes.  Just swallow the message and avoid crashing.
      }
#else
      // not supported; no way for us to restore the prompt, so just swallow the message
#endif
    }

    void cli_impl::process_commands(std::istream* input_stream)
    {  try {
      FC_ASSERT( input_stream != nullptr );
      _input_stream = input_stream;
      //force flushing to console and log file whenever input is read
      _input_stream->tie( _out );
      string line = get_line(get_prompt());
      while (_input_stream->good() && !_quit )
      {
        if (!execute_command_line(line))
          break;
        if( !_quit )
          line = get_line( get_prompt() );
      } // while cin.good
      wlog( "process commands exiting" );
    }  FC_CAPTURE_AND_RETHROW() }

    std::string cli_impl::time_to_string(const fc::time_point_sec& time)
    {
      if (_filter_output_for_tests)
        return "[time redacted]";
      boost::posix_time::ptime posix_time = boost::posix_time::from_time_t(time_t(time.sec_since_epoch()));
      return boost::posix_time::to_iso_extended_string(posix_time);
    }

  } // end namespace detail

   cli::cli( bts::client::client* client, std::istream* command_script, std::ostream* output_stream)
  :my( new detail::cli_impl(client,command_script,output_stream) )
  {
    my->_self = this;
  }

  void cli::set_input_stream_log(boost::optional<std::ostream&> input_stream_log)
  {
    my->_input_stream_log = input_stream_log;
  } 

  //disable reading from std::cin
  void cli::set_daemon_mode(bool daemon_mode) { my->_daemon_mode = daemon_mode; }
 
  void cli::display_status_message(const std::string& message)
  {
    my->display_status_message(message);
  }
 
  void cli::process_commands(std::istream* input_stream)
  {
    ilog( "starting to process interactive commands" );
    my->process_commands(input_stream);
  }

  cli::~cli()
  {
    try
    {
      wait_till_cli_shutdown();
    }
    catch( const fc::exception& e )
    {
      wlog( "${e}", ("e",e.to_detail_string()) );
    }
  }

  void cli::start() { my->start(); }

  void cli::wait_till_cli_shutdown()
  {
     ilog( "waiting on server to quit" );
     my->_rpc_server->wait_till_rpc_server_shutdown();
  }

  void cli::enable_output(bool enable_output)
  {
    if (!enable_output)
    { //save off original output and set output to nullstream
      my->_saved_out = my->_out;
      my->_out = &my->nullstream;
    }
    else
    { //can only enable output if it was previously disabled
      if (my->_saved_out)
      {
        my->_out = my->_saved_out;
        my->_saved_out = nullptr;
      }
    }
    
  }

  void cli::filter_output_for_tests(bool enable_flag)
  {
    my->_filter_output_for_tests = enable_flag;
  }

  bool cli::execute_command_line( const string& line, std::ostream* output)
  {
    auto old_output = my->_out;
    auto old_input = my->_input_stream;
    bool result = false;
    if( output != &my->nullstream )
    {
        my->_out = output;
        my->_input_stream = nullptr;
    }
    result = my->execute_command_line(line);
    if( output != &my->nullstream)
    {
        my->_out = old_output;
        my->_input_stream = old_input;
    }
    return result;
  }

  fc::variant cli::parse_argument_of_known_type(fc::buffered_istream& argument_stream,
                                                const bts::api::method_data& method_data,
                                                unsigned parameter_index)
  {
    return my->parse_argument_of_known_type(argument_stream, method_data, parameter_index);
  }
  fc::variants cli::parse_unrecognized_interactive_command(fc::buffered_istream& argument_stream,
                                                           const string& command)
  {
    return my->parse_unrecognized_interactive_command(argument_stream, command);
  }
  fc::variants cli::parse_recognized_interactive_command(fc::buffered_istream& argument_stream,
                                                         const bts::api::method_data& method_data)
  {
    return my->parse_recognized_interactive_command(argument_stream, method_data);
  }
  fc::variants cli::parse_interactive_command(fc::buffered_istream& argument_stream, const string& command)
  {
    return my->parse_interactive_command(argument_stream, command);
  }
  fc::variant cli::execute_interactive_command(const string& command, const fc::variants& arguments)
  {
    return my->execute_interactive_command(command, arguments);
  }
  void cli::format_and_print_result(const string& command,
                                    const fc::variants& arguments,
                                    const fc::variant& result)
  {
    return my->format_and_print_result(command, arguments, result);
  }

} } // bts::cli
