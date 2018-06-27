/*******************************************************************************
* Copyright 2017-2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include <dlfcn.h>
#include <sstream>

#include "ngraph/file_util.hpp"
#include "ngraph/runtime/backend.hpp"
#include "ngraph/runtime/cpu/cpu_tensor_view.hpp"
#include "ngraph/util.hpp"

using namespace std;
using namespace ngraph;

runtime::Backend::~Backend()
{
}

// This doodad finds the full path of the containing shared library
static string find_my_file()
{
    Dl_info dl_info;
    dladdr(reinterpret_cast<void*>(find_my_file), &dl_info);
    return dl_info.dli_fname;
}

// This will be uncommented when we add support for listing all known backends
// static bool is_backend(const string& path)
// {
//     bool rc = false;
//     string name = file_util::get_file_name(path);
//     if (name.find("_backend.") != string::npos)
//     {
//         NGRAPH_INFO << name;
//     }
//     return rc;
// }

void* runtime::Backend::open_shared_library(string type)
{
    string ext = SHARED_LIB_EXT;
    string ver = LIBRARY_VERSION;

    void* handle = nullptr;

    // strip off attributes, IE:CPU becomes IE
    auto colon = type.find(":");
    if (colon != type.npos)
    {
        type = type.substr(0, colon);
    }
    string lib_name = "lib" + to_lower(type) + "_backend" + ext;
    string my_directory = file_util::get_directory(find_my_file());
    string full_path = file_util::path_join(my_directory, lib_name);
    handle = dlopen(full_path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle)
    {
        string err = dlerror();
        throw runtime_error("Library open for Backend '" + lib_name + "' failed with error:\n" +
                            err);
    }
    return handle;
}

shared_ptr<runtime::Backend> runtime::Backend::create(const string& type)
{
    shared_ptr<runtime::Backend> rc;
    void* handle = open_shared_library(type);
    if (!handle)
    {
        throw runtime_error("Backend '" + type + "' not found");
    }
    else
    {
        function<runtime::Backend*(const char*)> new_backend =
            reinterpret_cast<runtime::Backend* (*)(const char*)>(dlsym(handle, "new_backend"));
        if (!new_backend)
        {
            dlclose(handle);
            throw runtime_error("Backend '" + type + "' does not implement new_backend");
        }

        function<void(runtime::Backend*)> delete_backend =
            reinterpret_cast<void (*)(runtime::Backend*)>(dlsym(handle, "delete_backend"));
        if (!delete_backend)
        {
            dlclose(handle);
            throw runtime_error("Backend '" + type + "' does not implement delete_backend");
        }

        runtime::Backend* backend = new_backend(type.c_str());
        rc = shared_ptr<runtime::Backend>(backend, [=](runtime::Backend* b) { delete_backend(b); });
    }
    return rc;
}

vector<string> runtime::Backend::get_registered_devices()
{
    return vector<string>();
}

void runtime::Backend::remove_compiled_function(shared_ptr<Function> func)
{
}

vector<ngraph::runtime::PerformanceCounter>
    runtime::Backend::get_performance_data(shared_ptr<Function> func) const
{
    return vector<PerformanceCounter>();
}

void runtime::Backend::validate_call(shared_ptr<const Function> function,
                                     const vector<shared_ptr<runtime::TensorView>>& outputs,
                                     const vector<shared_ptr<runtime::TensorView>>& inputs)
{
    const op::ParameterVector& input_parameters = function->get_parameters();
    if (input_parameters.size() != inputs.size())
    {
        stringstream ss;
        ss << "Call input count " << inputs.size() << " does not match Function's Parameter count "
           << input_parameters.size();
        throw runtime_error(ss.str());
    }
    if (function->get_output_size() != outputs.size())
    {
        stringstream ss;
        ss << "Call output count " << outputs.size() << " does not match Function's Result count "
           << function->get_output_size();
        throw runtime_error(ss.str());
    }

    for (size_t i = 0; i < input_parameters.size(); i++)
    {
        if (input_parameters[i]->get_element_type() != inputs[i]->get_tensor().get_element_type())
        {
            stringstream ss;
            ss << "Input " << i << " type '" << inputs[i]->get_tensor().get_element_type()
               << "' does not match Parameter type '" << input_parameters[i]->get_element_type()
               << "'";
            throw runtime_error(ss.str());
        }
        if (input_parameters[i]->get_shape() != inputs[i]->get_shape())
        {
            stringstream ss;
            ss << "Input " << i << " shape {" << join(inputs[i]->get_shape())
               << "} does not match Parameter shape {" << join(input_parameters[i]->get_shape())
               << "}";
            throw runtime_error(ss.str());
        }
    }

    for (size_t i = 0; i < function->get_output_size(); i++)
    {
        if (function->get_output_element_type(i) != outputs[i]->get_tensor().get_element_type())
        {
            stringstream ss;
            ss << "Output " << i << " type '" << outputs[i]->get_tensor().get_element_type()
               << "' does not match Parameter type '" << function->get_output_element_type(i)
               << "'";
            throw runtime_error(ss.str());
        }
        if (function->get_output_shape(i) != outputs[i]->get_shape())
        {
            stringstream ss;
            ss << "Output " << i << " shape {" << join(outputs[i]->get_shape())
               << "} does not match Parameter shape {" << join(function->get_output_shape(i))
               << "}";
            throw runtime_error(ss.str());
        }
    }
}
