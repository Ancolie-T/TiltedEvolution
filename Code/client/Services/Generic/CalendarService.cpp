#include <Services/CalendarService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/ServerTimeSettings.h>
#include <World.h>

#include <Forms/TESObjectCELL.h>
#include <PlayerCharacter.h>
#include <TimeManager.h>

constexpr float kTransitionSpeed = 5.f;

bool CalendarService::s_gameClockLocked = false;

bool CalendarService::AllowGameTick() noexcept
{
    return !s_gameClockLocked;
}

CalendarService::CalendarService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport)
    : m_world(aWorld), m_transport(aTransport)
{
    m_timeUpdateConnection = aDispatcher.sink<ServerTimeSettings>().connect<&CalendarService::OnTimeUpdate>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&CalendarService::HandleUpdate>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&CalendarService::OnDisconnected>(this);
}

void CalendarService::OnTimeUpdate(const ServerTimeSettings& acMessage) noexcept
{
    // disable the game clock
    ToggleGameClock(false);
    m_onlineTime.m_timeModel.TimeScale = acMessage.timeModel.TimeScale;
    m_onlineTime.m_timeModel.Time = acMessage.timeModel.Time;

    if (m_world.GetServerSettings().SyncPlayerCalendar)
    {
        m_onlineTime.m_timeModel.Day = acMessage.timeModel.Day;
        m_onlineTime.m_timeModel.Month = acMessage.timeModel.Month;
        m_onlineTime.m_timeModel.Year = acMessage.timeModel.Year;
    }
    else
    {
        m_onlineTime.m_timeModel.Day = m_offlineTime.m_timeModel.Day;
        m_onlineTime.m_timeModel.Month = m_offlineTime.m_timeModel.Month;
        m_onlineTime.m_timeModel.Year = m_offlineTime.m_timeModel.Year;
    }
}

void CalendarService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    // signal a time transition
    m_fadeTimer = 0.f;
    ToggleGameClock(true);
}

float CalendarService::TimeInterpolate(const TimeModel& aFrom, TimeModel& aTo) const
{
    const auto t = aTo.Time - aFrom.Time;
    if (t < 0.f)
    {
        const auto v = t + 24.f;
        // interpolate on the time difference, not the time
        const auto x = TiltedPhoques::Lerp(0.f, v, m_fadeTimer / kTransitionSpeed) + aFrom.Time;

        return TiltedPhoques::Mod(x, 24.f);
    }

    return TiltedPhoques::Lerp(aFrom.Time, aTo.Time, m_fadeTimer / kTransitionSpeed);
}

void CalendarService::ToggleGameClock(bool aEnable)
{
    auto* pGameTime = TimeData::Get();
    m_offlineTime.m_timeModel.Day = pGameTime->GameDay->f;
    m_offlineTime.m_timeModel.Month = pGameTime->GameMonth->f;
    m_offlineTime.m_timeModel.Year = pGameTime->GameYear->f;
    m_offlineTime.m_timeModel.Time = pGameTime->GameHour->f;
    m_offlineTime.m_timeModel.TimeScale = pGameTime->TimeScale->f;

    s_gameClockLocked = !aEnable;
}

void CalendarService::HandleUpdate(const UpdateEvent& aEvent) noexcept
{
    if (s_gameClockLocked)
    {
        const auto updateDelta = static_cast<float>(aEvent.Delta);
        auto* pGameTime = TimeData::Get();

        if (!m_lastTick)
            m_lastTick = m_world.GetTick();

        const auto now = m_world.GetTick();

        // we got disconnected or the client got ahead of us
        if (now < m_lastTick)
            return;

        const auto delta = now - m_lastTick;
        m_lastTick = now;

        m_onlineTime.Update(delta);
        pGameTime->TimeScale->f = m_onlineTime.m_timeModel.TimeScale;
        pGameTime->GameDay->f = m_onlineTime.m_timeModel.Day;
        pGameTime->GameMonth->f = m_onlineTime.m_timeModel.Month;
        pGameTime->GameYear->f = m_onlineTime.m_timeModel.Year;
        pGameTime->GameDaysPassed->f += m_onlineTime.GetDeltaTime(delta);

        // time transition in
        if (m_fadeTimer < kTransitionSpeed)
        {
            pGameTime->GameHour->f = TimeInterpolate(m_offlineTime.m_timeModel, m_onlineTime.m_timeModel);
            m_fadeTimer += updateDelta;
        }
        else
            pGameTime->GameHour->f = m_onlineTime.m_timeModel.Time;
    }
}
