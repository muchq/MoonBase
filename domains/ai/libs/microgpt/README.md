# MicroGPT

MicroGPT is a minimal, dependency-free implementation of a Generative Pre-trained Transformer (GPT) in pure Python. It is designed for educational purposes to understand the core algorithms behind GPT models.

## How it Works

The implementation is contained in a single file `microgpt.py` and includes:

1.  **Autograd Engine**: A `Value` class that implements a scalar-valued autograd engine. It builds a computation graph dynamically and supports backpropagation to calculate gradients.
2.  **Transformer Architecture**:
    -   **Embeddings**: Token embeddings (`wte`) and position embeddings (`wpe`).
    -   **Blocks**: Layers containing Multi-Head Attention and Feed-Forward (MLP) blocks.
    -   **Normalization**: RMSNorm is used for normalization.
    -   **Attention**: Standard Scaled Dot-Product Attention.
    -   **Activation**: ReLU activation.
3.  **Training Loop**:
    -   Uses the Adam optimizer.
    -   Trains on a simple dataset (names from `names.txt`).
    -   Calculates Cross-Entropy Loss.
4.  **Inference**:
    -   Generates text by predicting the next token based on the previous context.
    -   Uses sampling with temperature to control diversity.

## Running the Code

You can run the script directly:

```bash
python3 domains/ai/libs/microgpt/microgpt.py
```

Or using Bazel:

```bash
bazel run //domains/ai/libs/microgpt:microgpt
```

*Note: The script attempts to download `names.txt` to the current working directory if it doesn't exist. Ensure you have internet access or provide the file manually.*

## Import Warning

The `microgpt.py` file executes the training loop and inference immediately upon import. It is not designed to be imported as a library without modification (e.g., wrapping the execution logic in `if __name__ == "__main__":`).

## Inference in a Service

To use this model for inference in a production service, you would typically follow these steps:

### 1. Separate Concerns
Refactor `microgpt.py` to separate the model definition, training logic, and inference logic into different modules or functions. Specifically, ensure that the training loop only runs when the script is executed directly, not when imported.

### 2. Model Persistence
Currently, the model trains from scratch every time it runs. For a service:
-   **Save Weights**: After training, extract the `data` (float values) from the `Value` objects in `state_dict` and save them to a file (e.g., JSON, pickle, or a binary format).
-   **Load Weights**: In the service application, initialize the model structure and load the saved weights into the parameters.

### 3. Optimization
The `Value` class is designed for training (tracking gradients). For inference:
-   You don't need to track gradients. You can implement a lightweight version of the model that operates on plain floats or NumPy arrays (if dependencies are allowed) for faster execution.
-   Remove the backward pass logic.

### 4. API Implementation
Wrap the inference logic in a web server (e.g., using Flask, FastAPI, or a simple HTTP server).

**Example Workflow:**

1.  **Initialize**: Load the model parameters from disk into memory.
2.  **Request**: Receive a prompt (e.g., "Ma") from a client.
3.  **Tokenize**: Convert the prompt into token IDs.
4.  **Generate**: Run the `gpt` function in a loop to generate the next tokens until a termination condition (e.g., BOS token or max length) is met.
5.  **Detokenize**: Convert the generated token IDs back to a string.
6.  **Response**: Send the generated string back to the client.

### Example Service Structure (Conceptual)

```python
# service.py
import json
from microgpt_lib import gpt, state_dict, load_weights, tokenizer

# Load pre-trained weights
load_weights('model_weights.json')

def handle_request(prompt):
    token_ids = tokenizer.encode(prompt)
    generated_ids = generate(model, token_ids)
    return tokenizer.decode(generated_ids)

# ... server setup ...
```
