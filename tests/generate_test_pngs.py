#!/usr/bin/env python3
import json
import os
from PIL import Image, PngImagePlugin

os.makedirs("tests", exist_ok=True)

# 1. ComfyUI sample image with both prompt and workflow
comfy_prompt = {
    "3": {
        "class_type": "KSampler",
        "inputs": {
            "cfg": 8.0,
            "denoise": 1.0,
            "latent_image": ["5", 0],
            "model": ["4", 0],
            "negative": ["7", 0],
            "positive": ["6", 0],
            "sampler_name": "euler",
            "scheduler": "normal",
            "seed": 1234567890,
            "steps": 25
        }
    },
    "6": {
        "class_type": "CLIPTextEncode",
        "inputs": {
            "clip": ["4", 1],
            "text": "masterpiece, high quality, cinematic shot of an astronaut riding a horse on Mars, neon lighting, highly detailed, 8k"
        }
    },
    "7": {
        "class_type": "CLIPTextEncode",
        "inputs": {
            "clip": ["4", 1],
            "text": "low quality, blurry, deformed, distorted, watermark, signature"
        }
    }
}

comfy_workflow = {
    "nodes": [
        {"id": 3, "type": "KSampler", "pos": [800, 200]},
        {"id": 6, "type": "CLIPTextEncode", "pos": [400, 100], "title": "Positive Prompt"},
        {"id": 7, "type": "CLIPTextEncode", "pos": [400, 300], "title": "Negative Prompt"}
    ],
    "links": [[1, 6, 0, 3, 0, "CONDITIONING"], [2, 7, 0, 3, 1, "CONDITIONING"]],
    "version": 0.4
}

# Generate 64x64 test image
img = Image.new("RGB", (64, 64), color=(30, 40, 60))

# Save with standard tEXt metadata
meta_tEXt = PngImagePlugin.PngInfo()
meta_tEXt.add_text("prompt", json.dumps(comfy_prompt, indent=2))
meta_tEXt.add_text("workflow", json.dumps(comfy_workflow, indent=2))
img.save("tests/sample_comfy.png", pnginfo=meta_tEXt)
print("Generated tests/sample_comfy.png")

# Save with iTXt metadata (uncompressed UTF-8)
meta_iTXt = PngImagePlugin.PngInfo()
meta_iTXt.add_itxt("prompt", json.dumps(comfy_prompt), lang="", tkey="")
img.save("tests/sample_itxt.png", pnginfo=meta_iTXt)
print("Generated tests/sample_itxt.png")

# Save with workflow only
meta_wf = PngImagePlugin.PngInfo()
meta_wf.add_text("workflow", json.dumps(comfy_workflow, indent=2))
img.save("tests/sample_workflow_only.png", pnginfo=meta_wf)
print("Generated tests/sample_workflow_only.png")

# Save image with no ComfyUI metadata
img.save("tests/sample_no_meta.png")
print("Generated tests/sample_no_meta.png")

# Corrupt file
with open("tests/sample_corrupt.png", "wb") as f:
    f.write(b"NOT A PNG FILE DATA")
print("Generated tests/sample_corrupt.png")
