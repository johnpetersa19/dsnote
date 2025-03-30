/* Copyright (C) 2024 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "whisperspeech_engine.hpp"

#include <fmt/format.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <utility>

#include "cpu_tools.hpp"
#include "logger.hpp"
#include "py_executor.hpp"

using namespace pybind11::literals;

whisperspeech_engine::whisperspeech_engine(config_t config, callbacks_t call_backs)
    : tts_engine{std::move(config), std::move(call_backs)} {
    if ((cpu_tools::cpuinfo().feature_flags &
         cpu_tools::feature_flags_t::avx) == 0) {
        LOGE("avx not supported but whisperspeech engine needs it");
        throw std::runtime_error(
            "failed to init whisperspeech engine: avx not supported");
    }

    if (m_config.model_files.hub_path.empty()) {
        LOGE("hub path missing but whisperspeech engine needs it");
        throw std::runtime_error(
            "failed to init whisperspeech engine: no hub path");
    }
}

whisperspeech_engine::~whisperspeech_engine() {
    LOGD("whisperspeech dtor");

    stop();
}

void whisperspeech_engine::stop() {
    tts_engine::stop();

    auto task = py_executor::instance()->execute([&]() {
        try {
            m_model.reset();
            m_speaker.reset();

            // release mem
            py::module_::import("gc").attr("collect")();
            py::module_::import("torch").attr("cuda").attr("empty_cache")();
        } catch (const std::exception& err) {
            LOGE("py error: " << err.what());
        }
        return std::any{};
    });

    if (task) task->get();

    LOGD("whisperspeech stopped");
}

static void make_torch_link(const char* model_name, const std::string& hub_path,
                            const std::string& cache_dir) {
    auto model_path = fmt::format("{}/torch_{}_hub", hub_path, model_name);
    auto ln_target = fmt::format("{}/checkpoints", cache_dir);
    remove(ln_target.c_str());
    LOGD("ln: " << model_path << " => " << ln_target);
    (void)symlink(model_path.c_str(), ln_target.c_str());
}

void whisperspeech_engine::create_model() {
    auto task = py_executor::instance()->execute([&]() {
        auto s2a_file =
            find_file_with_name_prefix(m_config.model_files.model_path, "s2a-");
        if (s2a_file.empty()) {
            LOGE("failed to find s2a model");
            return false;
        }

        auto t2s_file =
            find_file_with_name_prefix(m_config.model_files.model_path, "t2s-");
        if (t2s_file.empty()) {
            LOGE("failed to find t2s model");
            return false;
        }

        LOGD("model files: " << s2a_file << " " << t2s_file);

        auto use_cuda =
            m_config.use_gpu &&
            (py_executor::instance()->libs_availability->torch_cuda ||
             py_executor::instance()->libs_availability->torch_hip);

        LOGD("using device: " << (use_cuda ? "cuda" : "cpu") << " "
                              << m_config.gpu_device.id);

        make_torch_link("encodec_24khz", m_config.model_files.hub_path,
                        m_config.cache_dir);
        make_hf_link("speechbrain--spkrec-ecapa-voxceleb",
                     m_config.model_files.hub_path, m_config.cache_dir);
        make_hf_link("charactr--vocos-encodec-24khz",
                     m_config.model_files.hub_path, m_config.cache_dir);

        try {
            auto hub = py::module_::import("torch.hub");
            hub.attr("set_dir")(m_config.cache_dir);

            auto api = py::module_::import("whisperspeech.pipeline");

            m_model = api.attr("Pipeline")(
                "s2a_ref"_a = s2a_file, "t2s_ref"_a = t2s_file,
                "device"_a = (use_cuda ? "cuda" : "cpu"));

            if (!use_cuda) {
                auto torch = py::module_::import("torch");
                m_model->attr("t2s").attr("switch_dtypes")(
                    torch.attr("float32"));
                m_model->attr("s2a").attr("switch_dtypes")(
                    torch.attr("float32"));
            }
        } catch (const std::exception& err) {
            LOGE("py error: " << err.what());
            return false;
        }
        return true;
    });

    if (!task || !std::any_cast<bool>(task->get()))
        LOGE("failed to create whisperspeech model");
    else
        LOGD("whisperspeech model created");
}

bool whisperspeech_engine::model_created() const { return static_cast<bool>(m_model); }

void whisperspeech_engine::reset_ref_voice() {
    auto task = py_executor::instance()->execute([&]() {
        try {
            m_speaker.reset();
        } catch (const std::exception& err) {
            LOGE("py error: " << err.what());
        }
        return std::any{};
    });

    if (task) task->get();
}

bool whisperspeech_engine::encode_speech_impl(
    const std::string& text, [[maybe_unused]] unsigned int speed,
    const std::string& out_file) {
    auto task = py_executor::instance()->execute([&]() {
        try {
            if (!m_speaker && !m_ref_voice_wav_file.empty()) {
                m_speaker =
                    m_model->attr("extract_spk_emb")(m_ref_voice_wav_file);
            }

            auto atoks = m_model->attr("generate_atoks")(
                "text"_a = text,
                "lang"_a = m_config.lang_code.empty() ? m_config.lang
                                                      : m_config.lang_code,
                "speaker"_a =
                    m_speaker.value_or(static_cast<py::object>(py::none())),
                "step_callback"_a = py::cpp_function{[this]() {
                    if (is_shutdown()) throw py::value_error("engine shutdown");
                }});

            m_model->attr("vocoder").attr("decode_to_file")(out_file, atoks);
        } catch (const std::exception& err) {
            LOGE("py error: " << err.what());
            return false;
        }

        LOGD("voice synthesized successfully");
        return true;
    });

    if (!task || !std::any_cast<bool>(task->get())) return false;

    return true;
}

bool whisperspeech_engine::model_supports_speed() const { return false; }
