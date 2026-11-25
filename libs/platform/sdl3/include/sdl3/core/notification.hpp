#pragma once
/**
 * @file notification.hpp
 * @brief Notifications système (SDL_notification.h) : autorisation, affichage, retrait.
 */
#include <SDL3/SDL.h>

#include <core/core.hpp>

namespace sdl3 {

// ============================================================================
// Notifications système (SDL_notification.h)
// ============================================================================

/// Identifiant d'une notification affichée.
using NotificationID = SDL_NotificationID;

/// Priorité d'affichage d'une notification.
enum class NotificationPriority {
	LOW = SDL_NOTIFICATION_PRIORITY_LOW,			///< basse priorité
	NORMAL = SDL_NOTIFICATION_PRIORITY_NORMAL,		///< normale
	HIGH = SDL_NOTIFICATION_PRIORITY_HIGH,			///< haute
	CRITICAL = SDL_NOTIFICATION_PRIORITY_CRITICAL,	///< critique
};

/// Type d'action proposée sur une notification.
enum class NotificationActionType {
	BUTTON = SDL_NOTIFICATION_ACTION_TYPE_BUTTON, ///< bouton d'action
};

namespace notification {

// Constantes de propriétés (SDL_PROP_NOTIFICATION_*), exposées pour ShowWithProperties.
inline constexpr const char *PROP_ACTIONS_POINTER		= SDL_PROP_NOTIFICATION_ACTIONS_POINTER;	   ///< pointeur vers les actions
inline constexpr const char *PROP_ACTION_COUNT_NUMBER	= SDL_PROP_NOTIFICATION_ACTION_COUNT_NUMBER;  ///< nombre d'actions
inline constexpr const char *PROP_IMAGE_POINTER		= SDL_PROP_NOTIFICATION_IMAGE_POINTER;		   ///< image de la notification
inline constexpr const char *PROP_MESSAGE_STRING		= SDL_PROP_NOTIFICATION_MESSAGE_STRING;		   ///< corps du message
inline constexpr const char *PROP_PRIORITY_NUMBER		= SDL_PROP_NOTIFICATION_PRIORITY_NUMBER;	   ///< priorité
inline constexpr const char *PROP_REPLACES_NUMBER		= SDL_PROP_NOTIFICATION_REPLACES_NUMBER;	   ///< notification remplacée
inline constexpr const char *PROP_SOUND_STRING		= SDL_PROP_NOTIFICATION_SOUND_STRING;		   ///< son
inline constexpr const char *PROP_TRANSIENT_BOOLEAN	= SDL_PROP_NOTIFICATION_TRANSIENT_BOOLEAN;	   ///< notification éphémère
inline constexpr const char *PROP_TITLE_STRING		= SDL_PROP_NOTIFICATION_TITLE_STRING;		   ///< titre

/// Construit une action de type « bouton » (identifiant + libellé localisé).
[[nodiscard]] inline SDL_NotificationAction MakeButtonAction(const char *actionId, const char *actionLabel) {
	SDL_NotificationAction action{};
	action.button.type = SDL_NOTIFICATION_ACTION_TYPE_BUTTON;
	action.button.action_id = actionId;
	action.button.action_label = actionLabel;
	return action;
}

/// Demande l'autorisation d'afficher des notifications (peut être refusée par l'OS).
inline bool RequestPermission() { return SDL_RequestNotificationPermission(); }

/// Affiche une notification simple (priorité normale). Retourne l'ID ou 0 si refusée.
inline NotificationID Show(const char *title, const char *message = nullptr,
						   SDL_Surface *image = nullptr,
						   SDL_NotificationAction *actions = nullptr, int numActions = 0) {
	return SDL_ShowNotification(title, message, image, actions, numActions);
}

/// Affiche une notification paramétrée par un groupe de propriétés.
inline NotificationID ShowWithProperties(SDL_PropertiesID props) {
	return SDL_ShowNotificationWithProperties(props);
}

/// Retire une notification affichée.
inline bool Remove(NotificationID id) { return SDL_RemoveNotification(id); }

} // namespace notification

} // namespace sdl3
