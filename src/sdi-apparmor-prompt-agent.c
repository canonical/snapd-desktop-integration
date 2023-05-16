/*
 * Copyright (C) 2023 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "sdi-apparmor-prompt-agent.h"
#include "sdi-apparmor-prompt-dialog.h"

struct _SdiApparmorPromptAgent {
  GObject parent_instance;

  SnapdClient *client;
};

G_DEFINE_TYPE(SdiApparmorPromptAgent, sdi_apparmor_prompt_agent, G_TYPE_OBJECT)

#if 0
static gboolean handle_prompt(PromptAgent *prompt_agent,
                              GDBusMethodInvocation *invocation,
                              const gchar *path, GVariant *info,
                              gpointer user_data) {
  SdiApparmorPromptDialog *dialog =
      sdi_apparmor_prompt_dialog_new(prompt_agent, invocation, path, info);
  gtk_window_present(GTK_WINDOW(dialog));

  return TRUE;
}
#endif

static void sdi_apparmor_prompt_agent_dispose(GObject *object) {
  SdiApparmorPromptAgent *self = SDI_APPARMOR_PROMPT_AGENT(object);

  g_clear_object(&self->client);

  G_OBJECT_CLASS(sdi_apparmor_prompt_agent_parent_class)->dispose(object);
}

void sdi_apparmor_prompt_agent_init(SdiApparmorPromptAgent *self) {}

void sdi_apparmor_prompt_agent_class_init(SdiApparmorPromptAgentClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = sdi_apparmor_prompt_agent_dispose;
}

SdiApparmorPromptAgent *sdi_apparmor_prompt_agent_new(SnapdClient *client) {
  SdiApparmorPromptAgent *self =
      g_object_new(sdi_apparmor_prompt_agent_get_type(), NULL);
  self->client = g_object_ref(client);
  return self;
}

gboolean sdi_apparmor_prompt_agent_start(SdiApparmorPromptAgent *self,
                                         GError **error) {

  return TRUE;
}
