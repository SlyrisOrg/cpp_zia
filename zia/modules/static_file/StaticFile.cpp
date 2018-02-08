//
// Created by milerius on 07/02/18.
//

#include <zia/modules/static_file/StaticFile.hpp>
#include <core/lib/Alias.hpp>
#include <fstream>
#include <iterator>
#include <iostream>

namespace zia::module
{
    bool StaticFile::config([[maybe_unused]] const api::Conf &conf)
    {
        return true;
    }

    bool StaticFile::exec(api::HttpDuplex &http)
    {
        if (http.req.method == zia::api::http::Method::get) {
            if (!fs::exists(http.req.uri)) {
                http.resp.status = zia::api::http::common_status::not_found;
                http.resp.headers.emplace("Content-Length", "0");
                return true;
            }
            http.resp.status = zia::api::http::common_status::ok;
            std::ifstream ifs(http.req.uri);
            std::vector<char> v{std::istreambuf_iterator<char>(ifs),
                      std::istreambuf_iterator<char>()};
            for (auto cur : v) {
                http.resp.body.push_back(std::byte(cur));
            }
            http.resp.headers["Content-Length"] = std::to_string(http.resp.body.size());
        }
        return true;
    }

    zia::api::Module *create() noexcept
    {
        return new zia::module::StaticFile;
    }
}

lib_alias_function(zia::module::create, create);