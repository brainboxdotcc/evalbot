/**
 * D++ eval command example.
 * This is dangerous and for educational use only, here be dragons!
 */

#include <dpp/dpp.h>
#include <dpp/json.h>
#include <fmt/format.h>
#include <fstream>
#include <iostream>
/* We have to define this to make certain functions visible */
#ifndef _GNU_SOURCE
        #define _GNU_SOURCE
#endif
#include <link.h>
#include <dlfcn.h>
#include <evalbot/eval.h>
#include <evalbot/picosha2.h>

using json = nlohmann::json;

/* This is an example function you can expose to your eval command */
int test_function() {
	return 42;
}

dpp::snowflake home_server = 0;

/* Important: This code is for UNIX-like systems only, e.g.
 * Linux, BSD, OSX. It will NOT work on Windows!
 * Note for OSX you'll probably have to change all references
 * to .so to .dylib.
 */
int main()
{
	/* Setup the bot */
	json configdocument;
	std::ifstream configfile("../config.json");
	configfile >> configdocument;

	dpp::cluster bot(configdocument["token"]);
	home_server = std::stoull(configdocument["homeserver"].get<std::string>());

	bot.on_form_submit([&](const dpp::form_submit_t & event) {
		std::string cpp = std::get<std::string>(event.components[0].components[0].value);

		event.thinking(true);

		std::vector<unsigned char> hash(picosha2::k_digest_size);
		picosha2::hash256(cpp.begin(), cpp.end(), hash.begin(), hash.end());
		std::string sha_hash = picosha2::bytes_to_hex_string(hash.begin(), hash.end());
		/** 
		 * THIS IS CRITICALLY IMPORTANT!
		 * Never EVER make an eval command that isn't restricted to a specific developer by user id.
		 * With access to this command the person who invokes it has at best full control over
		 * your bot's user account and at worst, full control over your entire network!!!
		 * Eval commands are Evil (pun intended) and could even be considered a security
		 * vulnerability. YOU HAVE BEEN WARNED!
		 */
		if (event.command.usr.id != MY_DEVELOPER) {
			dpp::message m;
			event.edit_response(dpp::message().set_content("On the day i do this for you, Satan will be ice skating to work.").set_flags(dpp::m_ephemeral));
			return;
		}

		/* We start by creating a string that contains a cpp program for a simple library.
		 * The library will contain one exported function called so_exec() that is called
		 * containing the raw C++ code to eval.
		 */
		std::string code = "#include <iostream>\n\
			#include <string>\n\
			#include <map>\n\
			#include <unordered_map>\n\
			#include <stdint.h>\n\
			#include <dpp/dpp.h>\n\
			#include <dpp/nlohmann/json.hpp>\n\
			#include <dpp/fmt/format.h>\n\
			#include \"../../include/evalbot/eval.h\"\n\
			extern \"C\" std::string so_exec_" + sha_hash + "(dpp::cluster& bot, dpp::form_submit_t event) {\n\
				" + cpp + ";\n\
				return std::string();\n\
			}";

		/* Next we output this string full of C++ to a cpp file on disk.
		 * This code assumes the current directory is writeable. The file will have a
		 * unique name made from the user's id and the message id.
		 */
		std::string filename_base = fmt::format("tmp/{}", sha_hash);
		std::string source_filename = fmt::format("{}.cpp", filename_base);
		std::string so_filename = fmt::format("{}.so", filename_base);
		double compile_start = dpp::utility::time_f();

		std::function<void()> execute_shared_object = [&bot, sha_hash, event, filename_base, source_filename, so_filename, compile_start]() {

			double compile_time = dpp::utility::time_f() - compile_start;

			/* Now for the meat of the function. To actually load
			 * our shared object we use dlopen() to load it into the
			 * memory space of our bot. If dlopen() returns a nullptr,
			 * the shared object could not be loaded. The user probably
			 * did something odd with the symbols inside their eval.
			 */
			auto shared_object_handle = dlopen(fmt::format("{}/{}", getenv("PWD"), so_filename).c_str(), RTLD_NOW);
			if (!shared_object_handle) {
				const char *dlsym_error = dlerror();
				event.edit_response(dpp::message().set_content("Shared object load error: ```\n" +
					std::string(dlsym_error ? dlsym_error : "Unknown error") +"\n```").set_flags(dpp::m_ephemeral));
				return;
			}

			/* This type represents the "void so_exec()" function inside
			 * the shared object library file.
			 */
			using function_pointer = std::string(*)(dpp::cluster&, dpp::form_submit_t);

			/* Attempt to find the function called so_exec() inside the
			 * library we just loaded. If we can't find it, then the user
			 * did something really strange in their eval. Also note it's
			 * important we call dlerror() here to reset it before trying
			 * to use it a second time. It's weird-ass C code and is just
			 * like that.
			 */
			dlerror();
			std::string function_name = "so_exec_" + sha_hash;
			function_pointer exec_run = (function_pointer)dlsym(shared_object_handle, function_name.c_str());
			const char *dlsym_error = dlerror();
			if (dlsym_error) {
				event.edit_response(dpp::message().set_content("Shared object load error: ```\n" + std::string(dlsym_error) +"\n```").set_flags(dpp::m_ephemeral));
				dlclose(shared_object_handle);
				return;
			}

			/* Now we have a function pointer to our actual exec code in
			 * 'exec_run', so lets call it, and pass it a reference to
			 * the cluster, and also a copy of the message_create_t.
			 */
			double run_start = dpp::utility::time_f();
			std::string result;
			try {
				result = exec_run(bot, event);
			}
			catch (const std::exception& e) {
				result = std::string("Exception: ") + e.what();
			}
			double run_time = dpp::utility::time_f() - run_start;

			/* When we're done with a .so file we must always dlclose() it */
			dlclose(shared_object_handle);

			/* Output some statistics */
			std::string content = "Execution completed.\n";
			if (compile_start - compile_time != 0) {
				content += fmt::format("Compile time: {0:.03f}s\n", compile_time);
			}
			content += fmt::format("Execution time {0:.06f}s.\n", run_time);
			if (result.empty()) {
				content += "No value returned.";
			} else {
				content += fmt::format("Returned value: ```\n{0}\n```", result);
			}
			event.edit_response(dpp::message().set_content(content).set_flags(dpp::m_ephemeral));

		};
	
		std::ifstream f(so_filename.c_str(), std::fstream::in);
		if (f.good()) {
			auto t = std::thread([event, &bot, source_filename, so_filename, compile_start, execute_shared_object]() {
				execute_shared_object();
			});
			t.detach();
		} else {
			std::fstream code_file(source_filename, std::fstream::binary | std::fstream::out);
			if (!code_file.is_open()) {
				//bot.message_create(dpp::message(event.msg.channel_id, "Unable to create source file for `eval`"));
				return;
			}
			code_file << code;
			code_file.close();

			/* Now to actually compile the file. We use dpp::utility::exec to
			* invoke a compiler. This assumes you are using g++, and it is in your path.
			*/
			dpp::utility::exec("g++", {
				"-std=c++17",
				"-shared",	/* Build the output as a .so file */
				"-fPIC",
				fmt::format("-o{}", so_filename),
				fmt::format("{}", source_filename),
				"-ldpp",
				"-ldl"
			}, [event, &bot, source_filename, so_filename, compile_start, execute_shared_object](const std::string &output) {

				/* After g++ is ran we end up inside this lambda with the output as a string */

				/* Delete our cpp file, we don't need it any more */
				unlink(source_filename.c_str());

				/* On successful compilation g++ outputs nothing, so any output here is error output */
				if (output.length()) {
					event.edit_response(dpp::message().set_content("Compile error: ```\n" + output + "\n```").set_flags(dpp::m_ephemeral));
				} else {
					execute_shared_object();
				}
			});
		}
	});

	bot.on_ready([&](const dpp::ready_t & event) {
		/* Create a slash command and register it as a global command */
		dpp::slashcommand newcommand;
		newcommand.set_name("eval").set_description("Evaluate C++ code under a running bot instance").set_application_id(bot.me.id);
		bot.guild_command_create(newcommand, home_server);
	});

	bot.on_interaction_create([&bot](const dpp::interaction_create_t & event) {
		if (event.command.type == dpp::it_application_command) {
			dpp::command_interaction cmd_data = std::get<dpp::command_interaction>(event.command.data);
			/* Check for our /dialog command */
			if (cmd_data.name == "eval") {
				/* Instantiate an interaction_modal_response object */
				dpp::interaction_modal_response modal("eval_modal", "C++ code evaluation");
				/* Add a text component */
				modal.add_component(
					dpp::component().
					set_label("Type C++ function body").
					set_id("eval_id").
					set_type(dpp::cot_text).
					set_placeholder("return \"ok!\";").
					set_min_length(1).
					set_max_length(2000).
					set_text_style(dpp::text_paragraph)
				);
				/* Trigger the dialog box. All dialog boxes are ephemeral */
				event.dialog(modal);
			}
		}
	});

	/* A basic logger */
	bot.on_log([](const dpp::log_t & event) {
		if (event.severity > dpp::ll_trace) {
			std::cout << event.message << "\n";
		}
	}); 

	bot.start(false);
	return 0;
}

