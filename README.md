# Automated Program Repair Using an LLM
Augment an LLM's bug fixing capabilities by providing it information from a fuzzer (AFL) and GDB

## Setup
You will need an OpenAI API Key and afl++ to run this program.

## Usage
Just run the provided Python notebook on your own buggy code or the examples provided in the data folder.

## How it works
Our system automates the bug-fixing loop. It starts from a given C/C++ program, then: 

#### Preprocessing 

We clean the code by removing comments to ensure the LLM does not “cheat” by reading hints from annotations. We only perform this step in the initial iteration as we want the LLM to read the comments it created in the previous iterations. 

#### Input Generation via LLM 

We prompt the LLM to produce Python code that generates initial seed inputs for AFL. We then execute this Python code using exec to create the input files. The LLM is also asked whether the target program consumes input via stdin or a file; this distinction is crucial, as AFL uses @ for stdin and @@ for file inputs. 

#### Fuzzing with AFL 

We run AFL on the target program. We do this using the Python subprocess module and a bash script that runs AFL for a user-defined amount of time and terminates once that time is reached. Once AFL identifies crash-inducing inputs, we store these inputs for use in the repair prompt. 

#### GDB Stacktrace 

We run GDB on the crashed inputs to obtain stack traces. Similar to how we ran AFL we use a bash script to run GDB on the crashed inputs. The number of crashed inputs and stack traces put in the prompt is limited to 5, so as to not confuse the LLM with excessive information. This information helps the LLM localize the offending line(s), providing a more direct clue about the bug’s origin. 

#### LLM-Based Repair 

Using a ChatGPT-based model (e.g., gpt-4o mini) integrated through LangChain, we supply the cleaned code, the crash-inducing inputs, and the GDB stacktrace to the LLM. It then proposes a patched version of the code. If the patch fails, we iterate up to three times. Memory is preserved across iterations—older attempts are trimmed if we hit the maximum token length, but the LLM maintains context of its previous attempts. If the bug is not fixed in three attempts, we consider it a failure. 
