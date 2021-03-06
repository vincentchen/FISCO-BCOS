/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file SealerPrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */

#include "LevelDBStorage.h"
#include "Table.h"
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <libdevcore/easylog.h>
#include <memory>

using namespace dev;
using namespace dev::storage;

Entries::Ptr LevelDBStorage::select(h256, int, const std::string& table, const std::string& key)
{
    try
    {
        std::string entryKey = table + "_" + key;
        std::string value;
        ReadGuard l(m_remoteDBMutex);
        auto s = m_db->Get(leveldb::ReadOptions(), leveldb::Slice(entryKey), &value);
        if (!s.ok() && !s.IsNotFound())
        {
            STORAGE_LEVELDB_LOG(ERROR)
                << LOG_DESC("Query leveldb failed") << LOG_KV("status", s.ToString());

            BOOST_THROW_EXCEPTION(StorageException(-1, "Query leveldb exception:" + s.ToString()));
        }

        Entries::Ptr entries = std::make_shared<Entries>();
        if (!s.IsNotFound())
        {
            // parse json
            std::stringstream ssIn;
            ssIn << value;

            Json::Value valueJson;
            ssIn >> valueJson;

            Json::Value values = valueJson["values"];
            for (auto it = values.begin(); it != values.end(); ++it)
            {
                Entry::Ptr entry = std::make_shared<Entry>();

                for (auto valueIt = it->begin(); valueIt != it->end(); ++valueIt)
                {
                    entry->setField(valueIt.key().asString(), valueIt->asString());
                }

                if (entry->getStatus() == Entry::Status::NORMAL)
                {
                    entry->setDirty(false);
                    entries->addEntry(entry);
                }
            }
        }

        return entries;
    }
    catch (std::exception& e)
    {
        STORAGE_LEVELDB_LOG(ERROR) << LOG_DESC("Query leveldb exception")
                                   << LOG_KV("msg", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(e);
    }

    return Entries::Ptr();
}

size_t LevelDBStorage::commit(
    h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas, h256 const&)
{
    try
    {
        std::shared_ptr<dev::db::LevelDBWriteBatch> batch = m_db->createWriteBatch();
        size_t total = 0;
        for (auto& it : datas)
        {
            for (auto& dataIt : it->data)
            {
                if (dataIt.second->size() == 0u)
                {
                    continue;
                }
                std::string entryKey = it->tableName + "_" + dataIt.first;
                Json::Value entry;

                for (size_t i = 0; i < dataIt.second->size(); ++i)
                {
                    Json::Value value;
                    for (auto& fieldIt : *(dataIt.second->get(i)->fields()))
                    {
                        value[fieldIt.first] = fieldIt.second;
                    }
                    value["_hash_"] = hash.hex();
                    value["_num_"] = num;
                    entry["values"].append(value);
                }

                std::stringstream ssOut;
                ssOut << entry;

                batch->insertSlice(leveldb::Slice(entryKey), leveldb::Slice(ssOut.str()));
                ++total;
                ssOut.seekg(0, std::ios::end);
                STORAGE_LEVELDB_LOG(TRACE)
                    << LOG_KV("commit key", entryKey) << LOG_KV("entries", dataIt.second->size())
                    << LOG_KV("len", ssOut.tellg());
            }
        }

        leveldb::WriteOptions writeOptions;
        writeOptions.sync = false;
        WriteGuard l(m_remoteDBMutex);
        auto s = m_db->Write(writeOptions, &(batch->writeBatch()));
        if (!s.ok())
        {
            STORAGE_LEVELDB_LOG(ERROR)
                << LOG_DESC("Commit leveldb failed") << LOG_KV("status", s.ToString());

            BOOST_THROW_EXCEPTION(StorageException(-1, "Commit leveldb exception:" + s.ToString()));
        }

        return total;
    }
    catch (std::exception& e)
    {
        STORAGE_LEVELDB_LOG(ERROR) << LOG_DESC("Commit leveldb exception")
                                   << LOG_KV("msg", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(e);
    }

    return 0;
}

bool LevelDBStorage::onlyDirty()
{
    return false;
}

void LevelDBStorage::setDB(std::shared_ptr<dev::db::BasicLevelDB> db)
{
    m_db = db;
}
