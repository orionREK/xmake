--!A cross-platform build utility based on Lua
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.
--
-- Copyright (C) 2015-2020, TBOOX Open Source Group.
--
-- @author      ruki
-- @file        fetch.lua
--

-- imports
import("core.base.option")
import("core.base.hashset")
import("core.project.project")
import("core.package.package", {alias = "core_package"})
import("core.tool.linker")
import("core.tool.compiler")
import("impl.package")
import("impl.repository")
import("impl.utils.get_requires")

-- fetch the given package info
function main(requires_raw)

    -- get requires and extra config
    local requires_extra = nil
    local requires, requires_extra = get_requires(requires_raw)
    if not requires or #requires == 0 then
        return
    end

    -- get the fetching modes
    local fetchmodes = option.get("fetch_modes")
    if fetchmodes then
        fetchmodes = hashset.from(fetchmodes:split(',', {plain = true}))
    end

    -- fetch all packages
    local fetchinfos = {}
    for _, instance in ipairs(package.load_packages(requires, {requires_extra = requires_extra, nodeps = true})) do
        local fetchinfo
        if fetchmodes and fetchmodes:has("deps") then
            fetchinfo = instance:fetchdeps()
        else
            fetchinfo = instance:fetch()
        end
        if fetchinfo then
            table.insert(fetchinfos, fetchinfo)
        end
    end

    -- show results
    if #fetchinfos > 0 then
        local flags = {}
        if fetchmodes and fetchmodes:has("cflags") then
            for _, fetchinfo in ipairs(fetchinfos) do
                table.join2(flags, compiler.map_flags("cxx", "define", fetchinfo.defines))
                table.join2(flags, compiler.map_flags("cxx", "includedir", fetchinfo.includedirs))
                for _, cflag in ipairs(fetchinfo.cflags) do
                    table.insert(flags, cflag)
                end
                for _, cxflag in ipairs(fetchinfo.cxflags) do
                    table.insert(flags, cxflag)
                end
                for _, cxxflag in ipairs(fetchinfo.cxxflags) do
                    table.insert(flags, cxxflag)
                end
            end
        end
        if fetchmodes and fetchmodes:has("ldflags") then
            for _, fetchinfo in ipairs(fetchinfos) do
                table.join2(flags, linker.map_flags("binary", {"cxx"}, "linkdir", fetchinfo.linkdirs))
                table.join2(flags, linker.map_flags("binary", {"cxx"}, "link", fetchinfo.links))
                for _, ldflag in ipairs(fetchinfo.ldflags) do
                    table.insert(flags, ldflags)
                end
            end
        end
        if #flags > 0 then
            print(table.concat(flags, " "))
        else
            print(fetchinfos)
        end
    end
end

