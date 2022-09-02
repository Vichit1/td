//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/EmojiStatus.h"

#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/Status.h"

namespace td {

struct EmojiStatuses {
  int64 hash_ = 0;
  vector<EmojiStatus> emoji_statuses_;

  td_api::object_ptr<td_api::premiumStatuses> get_premium_statuses_object() const {
    auto premium_statuses = transform(emoji_statuses_, [](const EmojiStatus &emoji_status) {
      CHECK(!emoji_status.is_empty());
      return emoji_status.get_premium_status_object();
    });

    return td_api::make_object<td_api::premiumStatuses>(std::move(premium_statuses));
  }

  EmojiStatuses() = default;

  explicit EmojiStatuses(tl_object_ptr<telegram_api::account_emojiStatuses> &&emoji_statuses) {
    CHECK(emoji_statuses != nullptr);
    hash_ = emoji_statuses->hash_;
    for (auto &status : emoji_statuses->statuses_) {
      EmojiStatus emoji_status(std::move(status));
      if (emoji_status.is_empty()) {
        LOG(ERROR) << "Receive empty premium status";
        continue;
      }
      if (emoji_status.get_until_date() != 0) {
        LOG(ERROR) << "Receive temporary premium status";
        emoji_status.clear_until_date();
      }
      emoji_statuses_.push_back(emoji_status);
    }
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(hash_, storer);
    td::store(emoji_statuses_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(hash_, parser);
    td::parse(emoji_statuses_, parser);
  }
};

static const string &get_default_emoji_statuses_database_key() {
  static string key = "def_emoji_statuses";
  return key;
}

static const string &get_recent_emoji_statuses_database_key() {
  static string key = "rec_emoji_statuses";
  return key;
}

static EmojiStatuses load_emoji_statuses(const string &key) {
  EmojiStatuses result;
  auto log_event_string = G()->td_db()->get_binlog_pmc()->get(key);
  if (!log_event_string.empty()) {
    log_event_parse(result, log_event_string).ensure();
  } else {
    result.hash_ = -1;
  }
  return result;
}

static void save_emoji_statuses(const string &key, const EmojiStatuses &emoji_statuses) {
  G()->td_db()->get_binlog_pmc()->set(key, log_event_store(emoji_statuses).as_slice().str());
}

class GetDefaultEmojiStatusesQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::premiumStatuses>> promise_;

 public:
  explicit GetDefaultEmojiStatusesQuery(Promise<td_api::object_ptr<td_api::premiumStatuses>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int64 hash) {
    send_query(G()->net_query_creator().create(telegram_api::account_getDefaultEmojiStatuses(hash), {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getDefaultEmojiStatuses>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto emoji_statuses_ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetDefaultEmojiStatusesQuery: " << to_string(emoji_statuses_ptr);

    if (emoji_statuses_ptr->get_id() == telegram_api::account_emojiStatusesNotModified::ID) {
      if (promise_) {
        promise_.set_error(Status::Error(500, "Receive wrong server response"));
      }
      return;
    }

    CHECK(emoji_statuses_ptr->get_id() == telegram_api::account_emojiStatuses::ID);
    EmojiStatuses emoji_statuses(move_tl_object_as<telegram_api::account_emojiStatuses>(emoji_statuses_ptr));
    save_emoji_statuses(get_default_emoji_statuses_database_key(), emoji_statuses);

    if (promise_) {
      promise_.set_value(emoji_statuses.get_premium_statuses_object());
    }
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetRecentEmojiStatusesQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::premiumStatuses>> promise_;

 public:
  explicit GetRecentEmojiStatusesQuery(Promise<td_api::object_ptr<td_api::premiumStatuses>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int64 hash) {
    send_query(G()->net_query_creator().create(telegram_api::account_getRecentEmojiStatuses(hash), {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getRecentEmojiStatuses>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto emoji_statuses_ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetRecentEmojiStatusesQuery: " << to_string(emoji_statuses_ptr);

    if (emoji_statuses_ptr->get_id() == telegram_api::account_emojiStatusesNotModified::ID) {
      if (promise_) {
        promise_.set_error(Status::Error(500, "Receive wrong server response"));
      }
      return;
    }

    CHECK(emoji_statuses_ptr->get_id() == telegram_api::account_emojiStatuses::ID);
    EmojiStatuses emoji_statuses(move_tl_object_as<telegram_api::account_emojiStatuses>(emoji_statuses_ptr));
    save_emoji_statuses(get_recent_emoji_statuses_database_key(), emoji_statuses);

    if (promise_) {
      promise_.set_value(emoji_statuses.get_premium_statuses_object());
    }
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ClearRecentEmojiStatusesQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ClearRecentEmojiStatusesQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::account_clearRecentEmojiStatuses(), {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_clearRecentEmojiStatuses>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    save_emoji_statuses(get_recent_emoji_statuses_database_key(), EmojiStatuses());
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

EmojiStatus::EmojiStatus(const td_api::object_ptr<td_api::premiumStatus> &premium_status, int32 duration) {
  if (premium_status == nullptr) {
    return;
  }

  custom_emoji_id_ = premium_status->custom_emoji_id_;
  if (duration != 0) {
    int32 current_time = G()->unix_time();
    if (duration >= std::numeric_limits<int32>::max() - current_time) {
      until_date_ = std::numeric_limits<int32>::max();
    } else {
      until_date_ = current_time + duration;
    }
  }
}

EmojiStatus::EmojiStatus(tl_object_ptr<telegram_api::EmojiStatus> &&emoji_status) {
  if (emoji_status == nullptr) {
    return;
  }
  switch (emoji_status->get_id()) {
    case telegram_api::emojiStatusEmpty::ID:
      break;
    case telegram_api::emojiStatus::ID: {
      auto status = static_cast<const telegram_api::emojiStatus *>(emoji_status.get());
      custom_emoji_id_ = status->document_id_;
      break;
    }
    case telegram_api::emojiStatusUntil::ID: {
      auto status = static_cast<const telegram_api::emojiStatusUntil *>(emoji_status.get());
      custom_emoji_id_ = status->document_id_;
      until_date_ = status->until_;
      break;
    }
    default:
      UNREACHABLE();
  }
}

tl_object_ptr<telegram_api::EmojiStatus> EmojiStatus::get_input_emoji_status() const {
  if (is_empty()) {
    return make_tl_object<telegram_api::emojiStatusEmpty>();
  }
  if (until_date_ != 0) {
    return make_tl_object<telegram_api::emojiStatusUntil>(custom_emoji_id_, until_date_);
  }
  return make_tl_object<telegram_api::emojiStatus>(custom_emoji_id_);
}

td_api::object_ptr<td_api::premiumStatus> EmojiStatus::get_premium_status_object() const {
  if (is_empty()) {
    return nullptr;
  }
  return td_api::make_object<td_api::premiumStatus>(custom_emoji_id_);
}

int64 EmojiStatus::get_effective_custom_emoji_id(bool is_premium, int32 unix_time) const {
  if (!is_premium) {
    return 0;
  }
  if (until_date_ != 0 && until_date_ <= unix_time) {
    return 0;
  }
  return custom_emoji_id_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const EmojiStatus &emoji_status) {
  if (emoji_status.is_empty()) {
    return string_builder << "DefaultProfileBadge";
  }
  string_builder << "CustomEmoji " << emoji_status.custom_emoji_id_;
  if (emoji_status.until_date_ != 0) {
    string_builder << " until " << emoji_status.until_date_;
  }
  return string_builder;
}

void get_default_emoji_statuses(Td *td, Promise<td_api::object_ptr<td_api::premiumStatuses>> &&promise) {
  auto statuses = load_emoji_statuses(get_default_emoji_statuses_database_key());
  if (statuses.hash_ != -1 && promise) {
    promise.set_value(statuses.get_premium_statuses_object());
    promise = Promise<td_api::object_ptr<td_api::premiumStatuses>>();
  }
  td->create_handler<GetDefaultEmojiStatusesQuery>(std::move(promise))->send(statuses.hash_);
}

void get_recent_emoji_statuses(Td *td, Promise<td_api::object_ptr<td_api::premiumStatuses>> &&promise) {
  auto statuses = load_emoji_statuses(get_recent_emoji_statuses_database_key());
  if (statuses.hash_ != -1 && promise) {
    promise.set_value(statuses.get_premium_statuses_object());
    promise = Promise<td_api::object_ptr<td_api::premiumStatuses>>();
  }
  td->create_handler<GetRecentEmojiStatusesQuery>(std::move(promise))->send(statuses.hash_);
}

void add_recent_emoji_status(EmojiStatus emoji_status) {
  if (emoji_status.is_empty()) {
    return;
  }

  emoji_status.clear_until_date();
  auto statuses = load_emoji_statuses(get_recent_emoji_statuses_database_key());
  if (!statuses.emoji_statuses_.empty() && statuses.emoji_statuses_[0] == emoji_status) {
    return;
  }

  statuses.hash_ = 0;
  td::remove(statuses.emoji_statuses_, emoji_status);
  statuses.emoji_statuses_.insert(statuses.emoji_statuses_.begin(), emoji_status);
  constexpr size_t MAX_RECENT_EMOJI_STATUSES = 50;  // server-side limit
  if (statuses.emoji_statuses_.size() > MAX_RECENT_EMOJI_STATUSES) {
    statuses.emoji_statuses_.resize(MAX_RECENT_EMOJI_STATUSES);
  }
  save_emoji_statuses(get_recent_emoji_statuses_database_key(), statuses);
}

void clear_recent_emoji_statuses(Td *td, Promise<Unit> &&promise) {
  save_emoji_statuses(get_recent_emoji_statuses_database_key(), EmojiStatuses());
  td->create_handler<ClearRecentEmojiStatusesQuery>(std::move(promise))->send();
}

}  // namespace td