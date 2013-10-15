/* Message object used by text channel client-side proxy
 *
 * Copyright (C) 2009 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2009 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "Message"

#include <QDateTime>
#include <QPointer>
#include <QSet>

#include "TextChannel"

namespace Tp
{

struct Message::Private : public QSharedData
{
    Private(const MessagePartList &parts);
    ~Private();

    MessagePartList parts;

    ContactPtr sender;

    inline QVariant value(uint index, const char *key) const;
    inline uint getUIntOrZero(uint index, const char *key) const;
    inline QString getStringOrEmpty(uint index, const char *key) const;
    inline bool getBoolean(uint index, const char *key,
            bool assumeIfAbsent) const;
    inline uint senderHandle() const;
    inline uint pendingId() const;
    void clearSenderHandle();
};

Message::Private::Private(const MessagePartList &parts)
    : parts(parts),
      sender(0)
{
}

Message::Private::~Private()
{
}

inline QVariant Message::Private::value(uint index, const char *key) const
{
    return parts.at(index).value(QLatin1String(key)).variant();
}

inline QString Message::Private::getStringOrEmpty(uint index, const char *key)
    const
{
    QString s = value(index, key).toString();
    if (s.isNull()) {
        s = QLatin1String("");
    }
    return s;
}

inline uint Message::Private::getUIntOrZero(uint index, const char *key)
    const
{
    return value(index, key).toUInt();
}

inline bool Message::Private::getBoolean(uint index, const char *key,
        bool assumeIfAbsent) const
{
    QVariant v = value(index, key);
    if (v.isValid() && v.type() == QVariant::Bool) {
        return v.toBool();
    }
    return assumeIfAbsent;
}

inline uint Message::Private::senderHandle() const
{
    return getUIntOrZero(0, "message-sender");
}

/**
 * \class Message
 * \ingroup clientchannel
 * \headerfile TelepathyQt/text-channel.h <TelepathyQt/TextChannel>
 *
 * \brief The Message class represents a Telepathy message in a text channel.
 * These objects are implicitly shared, like QString.
 */

/**
 * Default constructor, only used internally.
 */
Message::Message()
{
}

/**
 * Constructor.
 *
 * \param parts The parts of a message as defined by the Telepathy D-Bus
 *              specification. This list must have length at least 1.
 */
Message::Message(const MessagePartList &parts)
    : mPriv(new Private(parts))
{
    Q_ASSERT(parts.size() > 0);
}

/**
 * Constructor, from the parameters of the old Sent signal.
 *
 * \param timestamp The time the message was sent
 * \param type The message type
 * \param text The text of the message
 */
Message::Message(uint timestamp, uint type, const QString &text)
    : mPriv(new Private(MessagePartList() << MessagePart() << MessagePart()))
{
    mPriv->parts[0].insert(QLatin1String("message-sent"),
            QDBusVariant(static_cast<qlonglong>(timestamp)));
    mPriv->parts[0].insert(QLatin1String("message-type"),
            QDBusVariant(type));

    mPriv->parts[1].insert(QLatin1String("content-type"),
            QDBusVariant(QLatin1String("text/plain")));
    mPriv->parts[1].insert(QLatin1String("content"), QDBusVariant(text));
}

/**
 * Constructor, from the parameters of the old Send method.
 *
 * \param type The message type
 * \param text The text of the message
 */
Message::Message(ChannelTextMessageType type, const QString &text)
    : mPriv(new Private(MessagePartList() << MessagePart() << MessagePart()))
{
    mPriv->parts[0].insert(QLatin1String("message-type"),
            QDBusVariant(static_cast<uint>(type)));

    mPriv->parts[1].insert(QLatin1String("content-type"),
            QDBusVariant(QLatin1String("text/plain")));
    mPriv->parts[1].insert(QLatin1String("content"), QDBusVariant(text));
}

/**
 * Copy constructor.
 */
Message::Message(const Message &other)
    : mPriv(other.mPriv)
{
}

/**
 * Assignment operator.
 */
Message &Message::operator=(const Message &other)
{
    if (this != &other) {
        mPriv = other.mPriv;
    }

    return *this;
}

/**
 * Equality operator.
 */
bool Message::operator==(const Message &other) const
{
    return this->mPriv == other.mPriv;
}

/**
 * Class destructor.
 */
Message::~Message()
{
}

/**
 * Return the time the message was sent, or QDateTime() if that time is
 * unknown.
 *
 * \return A timestamp
 */
QDateTime Message::sent() const
{
    // FIXME See http://bugs.freedesktop.org/show_bug.cgi?id=21690
    uint stamp = mPriv->value(0, "message-sent").toUInt();
    if (stamp != 0) {
        return QDateTime::fromTime_t(stamp);
    } else {
        return QDateTime();
    }
}

/**
 * Return the type of message this is, or ChannelTextMessageTypeNormal
 * if the type is not recognised.
 *
 * \return The ChannelTextMessageType for this message
 */
ChannelTextMessageType Message::messageType() const
{
    uint raw = mPriv->value(0, "message-type").toUInt();

    if (raw < static_cast<uint>(NUM_CHANNEL_TEXT_MESSAGE_TYPES)) {
        return ChannelTextMessageType(raw);
    } else {
        return ChannelTextMessageTypeNormal;
    }
}

/**
 * Return the unique token identifying this message (e.g. the id attribute
 * for XMPP messages), or an empty string if there is no suitable token.
 *
 * \return A non-empty message identifier, or an empty string if none
 */
QString Message::messageToken() const
{
    return mPriv->getStringOrEmpty(0, "message-token");
}

QString Message::text() const
{
    // Alternative-groups for which we've already emitted an alternative
    QSet<QString> altGroupsUsed;
    QString text;

    for (int i = 1; i < size(); i++) {
        QString altGroup = mPriv->getStringOrEmpty(i, "alternative");
        QString contentType = mPriv->getStringOrEmpty(i, "content-type");

        if (contentType == QLatin1String("text/plain")) {
            if (!altGroup.isEmpty()) {
                if (altGroupsUsed.contains(altGroup)) {
                    continue;
                } else {
                    altGroupsUsed << altGroup;
                }
            }

            QVariant content = mPriv->value(i, "content");
            if (content.type() == QVariant::String) {
                text += content.toString();
            } else {
            }
        }
    }

    return text;
}

/**
 * Return the message's header part, as defined by the Telepathy D-Bus API
 * specification. This is provided for advanced clients that need to access
 * additional information not available through the normal Message API.
 *
 * \return The same thing as messagepart(0)
 */
MessagePart Message::header() const
{
    return part(0);
}

/**
 * Return the number of parts in this message.
 *
 * \return 1 greater than the largest valid argument to part
 */
int Message::size() const
{
    return mPriv->parts.size();
}

/**
 * Return the message's header part, as defined by the Telepathy D-Bus API
 * specification. This is provided for advanced clients that need to access
 * additional information not available through the normal Message API.
 *
 * \param index The part to access, which must be strictly less than size();
 *              part number 0 is the header, parts numbered 1 or greater
 *              are the body of the message.
 * \return Part of the message
 */
MessagePart Message::part(uint index) const
{
    return mPriv->parts.at(index);
}

MessagePartList Message::parts() const
{
    return mPriv->parts;
}

MessagePart& Message::ut_part(uint index)
{
    return mPriv->parts[index];
}

/**
 * \class ReceivedMessage
 * \ingroup clientchannel
 * \headerfile TelepathyQt/text-channel.h <TelepathyQt/TextChannel>
 *
 * \brief The ReceivedMessage class is a subclass of Message, representing a
 * received message.
 *
 * It contains additional information that's generally only
 * available on received messages.
 */

/**
 * Default constructor, only used internally.
 */
ReceivedMessage::ReceivedMessage()
{
}

/**
 * Constructor.
 *
 * \param parts The parts of a message as defined by the Telepathy D-Bus
 *              specification. This list must have length at least 1.
 */
ReceivedMessage::ReceivedMessage(const MessagePartList &parts)
    : Message(parts)
{
    if (!mPriv->parts[0].contains(QLatin1String("message-received"))) {
        mPriv->parts[0].insert(QLatin1String("message-received"),
                QDBusVariant(static_cast<qlonglong>(
                        QDateTime::currentDateTime().toTime_t())));
    }
}

/**
 * Copy constructor.
 */
ReceivedMessage::ReceivedMessage(const ReceivedMessage &other)
    : Message(other)
{
}

/**
 * Assignment operator.
 */
ReceivedMessage &ReceivedMessage::operator=(const ReceivedMessage &other)
{
    if (this != &other) {
        mPriv = other.mPriv;
    }

    return *this;
}

/**
 * Destructor.
 */
ReceivedMessage::~ReceivedMessage()
{
}

/**
 * Return the time the message was received.
 *
 * \return A timestamp
 */
QDateTime ReceivedMessage::received() const
{
    // FIXME See http://bugs.freedesktop.org/show_bug.cgi?id=21690
    uint stamp = mPriv->value(0, "message-received").toUInt();
    if (stamp != 0) {
        return QDateTime::fromTime_t(stamp);
    } else {
        return QDateTime();
    }
}

/**
 * Return the Contact who sent the message, or
 * ContactPtr(0) if unknown.
 *
 * \return The sender or ContactPtr(0)
 */
ContactPtr ReceivedMessage::sender() const
{
    return mPriv->sender;
}

/**
 * Return whether the incoming message was part of a replay of message
 * history.
 *
 * If true, loggers can use this to improve their heuristics for elimination
 * of duplicate messages (a simple, correct implementation would be to avoid
 * logging any message that has this flag).
 *
 * \return whether the scrollback flag is set
 */
bool ReceivedMessage::isScrollback() const
{
    return mPriv->getBoolean(0, "scrollback", false);
}

void ReceivedMessage::ut_setSender(const ContactPtr& sender)
{
    mPriv->sender = sender;
}

} // Tp
